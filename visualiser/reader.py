import argparse
import struct
import pdb
import mmap
import numpy as np
import os
from datetime import datetime, timedelta

from flask import Flask, jsonify, render_template_string

def safe_div(a, b, default=0):
    try:
        return a / b
    except ZeroDivisionError:
        return default

def median(num, buckets):
    halfSamples = num / 2
    sum = 0
    for i, v in enumerate(buckets):
        sum += v
        if (sum >= halfSamples):
            return i
    return 0

def readDumpHistogram(f):
    header_data = f.read(8 * 7)
    _maxSample, _minSample, _overflows, _sum, _numSamples, _samplesPerBucket, _labelLen  = struct.unpack("<7Q", header_data)

    # label
    label_data = f.read(256)
    label = label_data[:_labelLen].decode('utf-8')

    # buckets
    header_data = f.read(8)
    bucketLen = struct.unpack("<1Q", header_data)[0]
    buckets_data = f.read(bucketLen * 8)
    buckets = struct.unpack(f"<{bucketLen}Q", buckets_data)

    desc = f"{label}\n"
    desc += f"#buckets: {bucketLen}, #samples: {_numSamples}, #overflows: {_overflows}, ns/bucket: {_samplesPerBucket}\n"

    meanNS = safe_div(_sum, _numSamples)
    meanUnits = safe_div(meanNS, _samplesPerBucket)
    _median = median(_numSamples, buckets)
    desc += f"mean: {meanUnits:.2f} ({meanNS:.2f} ns), median {_median}, min: {_minSample} ns, max: {_maxSample} ns"

    return (desc, buckets)

def readRateCounter(f):
    rawData = f.read(8)
    labelLen = struct.unpack("<Q", rawData)[0]
    label_data = f.read(256)
    label = label_data[:labelLen].decode('utf-8')

    rawData = f.read(8)
    countersLen = struct.unpack("<Q", rawData)[0]
    counters_data = f.read(countersLen * 8)
    counters = struct.unpack(f"<{countersLen}Q", counters_data)

    return (label, counters)

def readFile(fileName):
    with open(fileName, "rb") as f:
        header_data = f.read(8) # read id
        id = struct.unpack("<Q", header_data)[0]
        if id == 0x0101010101010101:
            return readDumpHistogram(f)
        elif id == 0x0202020202020202:
            return readRateCounter(f)
        else:
            return f"unknown magic id: {id}"

def runWebserver(fileName_):
    # HTML and JavaScript content
    HTML_TEMPLATE = """
<!DOCTYPE html>
<html>
<head>
    <title>@fileNameToReplace</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head>
<body>
    <div style="width: 80%; margin: auto;">
        <canvas id="myChart"></canvas>
    </div>
    <script>
        let myChart; // Variable to hold the chart instance

        async function updateChart() {
            try {
                const response = await fetch('/api/data');
                const data = await response.json();

                const ctx = document.getElementById('myChart').getContext('2d');

                if (!myChart) {
                    // Initial creation
                    myChart = new Chart(ctx, {
                        type: 'line',
                        data: {
                            labels: data.labels,
                            datasets: [{
                                label: data.description,
                                data: data.values,
                                borderColor: 'rgb(75, 192, 192)',
                                tension: 0.1,
                                pointRadius: 0 // Better performance for fast updates
                            }]
                        },
                        options: {
                            animation: false // Disable animations for 100ms updates
                        }
                    });
                } else {
                    // Update existing chart
                    myChart.data.labels = data.labels;
                    myChart.data.datasets[0].data = data.values;
                    myChart.data.datasets[0].label = data.description;
                    myChart.update('none'); // Use 'none' mode to skip animations
                }
            } catch (error) {
                console.error("Failed to fetch data:", error);
            }
        }

    // Run immediately, then every 100ms
    updateChart();
    setInterval(updateChart, 100);
    </script>
</body>
</html>
"""    
    print(f"using {fileName_}")
    HTML_CONTENT = HTML_TEMPLATE.replace('@fileNameToReplace', fileName_)
    def generate_data(fileName):
        desc, data = readFile(fileName)
        return {
            "labels": [i for i in range(0, len(data))],
            "values": list(data),
            "description": desc
        }

    app = Flask(__name__)

    @app.route('/')
    def index():
        # Serving a minimal HTML structure; JS will do the heavy lifting
        return render_template_string(HTML_CONTENT)

    @app.route('/api/data')
    def get_data():
        return jsonify(generate_data(fileName_))

    
    app.run(debug=True)

def generateDescriptionHistogram(header, data):
    lines = []

    samplesPerBucket = header['samplesPerBucket']
    maxSampleNS = header['maxSample']
    maxSample = safe_div(maxSampleNS, samplesPerBucket)
    minSampleNS = header['minSample']
    minSample = safe_div(minSampleNS, samplesPerBucket)
    
    lines.append(
        f"max: {maxSample:.2f} ({maxSampleNS:.2f} ns), "
        f"min: {minSample:.2f} ({minSampleNS:.2f} ns), "
    )

    lines.append(f"overflows: {header['overflows']}, "
                 f"numSamples: {header['numSamples']}, "
                 f"samplesPerBucket: {header['samplesPerBucket']}")

    meanNS = safe_div(header['sum'], header['numSamples'])
    meanUnits = safe_div(meanNS, samplesPerBucket)
    _median = median(header['numSamples'], data)

    lines.append(f"mean: {meanUnits:.2f}, mean: {meanNS:.2f} ns, median: {_median}")

    return "\n".join(lines)

def generateDescriptionRateCounter(header, data):
    lines = []
    sum = 0
    num = 0
    for i, v in enumerate(data):
        if v != 0:
            sum += v
            num += 1
    mean = safe_div(sum, num)
    lines.append(f"mean: {mean:.2f}")

    return "\n".join(lines)

def read_telemetry(fileName_, fd, mm, timeout=timedelta(seconds=1)):
    ID_HISTOGRAM = 0x0101010101010101
    ID_RATE_COUNTER = 0x0202020202020202

    ready_offset = 0
    header, descGenerator, data = None, None, None

    magicID = np.frombuffer(mm, dtype=np.uint64, count=1)[0]
    if magicID == ID_HISTOGRAM:
        dt = np.dtype([
            ('magicID', np.uint64), 
            ('maxSample', np.uint64),
            ('minSample', np.uint64), 
            ('overflows', np.uint64), 
            ('sum', np.uint64), 
            ('numSamples', np.uint64), 
            ('samplesPerBucket', np.uint64), 
            ('labelLen', np.uint64), 
            ('label', 'S256'),
            ('bucketLen', np.uint64), 
        ])

        header = np.frombuffer(mm, dtype=dt, count=1)[0]
        bucketLen = int(header['bucketLen'])
        buckets_offset = dt.itemsize
        data = np.frombuffer(mm, dtype=np.uint64, count=bucketLen, offset=buckets_offset)
        descGenerator = generateDescriptionHistogram
        ready_offset = buckets_offset + data.nbytes
    elif magicID == ID_RATE_COUNTER:
        dt = np.dtype([
            ('magicID', np.uint64), 
            ('labelLen', np.uint64),
            ('label', 'S256'),
            ('counterLen', np.uint64),
        ])
        header = np.frombuffer(mm, dtype=dt, count=1)[0]
        counterLen = int(header['counterLen'])
        
        rate_offset = dt.itemsize
        data = np.frombuffer(mm, dtype=np.uint64, count=counterLen, offset=rate_offset)
        descGenerator = generateDescriptionRateCounter
        ready_offset = rate_offset + data.nbytes

    else:
        raise Exception(f"unknown magicID: {id}")

    endTP = datetime.now() + timeout
    while datetime.now() < endTP:
        ready = np.frombuffer(mm, dtype=np.bool_, count=1, offset=ready_offset)[0]
        if ready:
            break
        time.sleep(0.01) # Short sleep for high-performance updates
    else:
        raise TimeoutError(f"SHM file {fileName_} was not ready within {timeout}")

    return header, descGenerator, data

def runWebserver_dataFromSharedMemory(fileName_):
    # HTML and JavaScript content
    HTML_TEMPLATE = """
<!DOCTYPE html>
<html>
<head>
    <title>@fileNameToReplace</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head>
<body>
    <div style="width: 80%; margin: auto;">
        <pre id="descriptionBox" style="font-family: sans-serif; padding: 10px; line-height: 1.5;"></pre>
        <canvas id="myChart"></canvas>
    </div>
    <script>
        let myChart; // Variable to hold the chart instance

        async function updateChart() {
            try {
                const response = await fetch('/api/data');
                const data = await response.json();

                const ctx = document.getElementById('myChart').getContext('2d');

                const descBox = document.getElementById('descriptionBox');
                if (descBox) {
                    descBox.innerText = data.description;
                }

                if (!myChart) {
                    // Initial creation
                    myChart = new Chart(ctx, {
                        type: 'line',
                        data: {
                            labels: data.labels,
                            datasets: [{
                                label: data.label,
                                data: data.values,
                                borderColor: 'rgb(75, 192, 192)',
                                tension: 0.1,
                                pointRadius: 0 // Better performance for fast updates
                            }]
                        },
                        options: {
                            animation: false // Disable animations for 100ms updates
                        }
                    });
                } else {
                    // Update existing chart
                    myChart.data.labels = data.labels;
                    myChart.data.datasets[0].data = data.values;
                    myChart.data.datasets[0].label = data.label;
                    myChart.update('none'); // Use 'none' mode to skip animations
                }
            } catch (error) {
                console.error("Failed to fetch data:", error);
            }
        }

    // Run immediately, then every 100ms
    updateChart();
    setInterval(updateChart, 100);
    </script>
</body>
</html>
"""
    print(f"using {fileName_}")
    HTML_CONTENT = HTML_TEMPLATE.replace('@fileNameToReplace', fileName_)
    def generate_data(header, descGenerator, label, data):
        return {
            "labels": list(range(len(data))),
            "values": data.tolist(),
            "label": label,
            "description": descGenerator(header, data)
        }

    app = Flask(__name__)

    @app.route('/')
    def index():
        # Serving a minimal HTML structure; JS will do the heavy lifting
        return render_template_string(HTML_CONTENT)
    
    fd = os.open(fileName_, os.O_RDONLY)
    mm = mmap.mmap(fd, length=0, access=mmap.ACCESS_READ)
    header, descGenerator, data = read_telemetry(fileName_, fd, mm, timedelta(seconds=1))
    label = header['label'][:int(header['labelLen'])].decode('utf-8')

    @app.route('/api/data')
    def get_data():
        return jsonify(generate_data(header, descGenerator, label, data))

    app.run(debug=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="A script to add two numbers.")

    # Add arguments
    parser.add_argument("file", type=str, help="file to read")
    parser.add_argument("--web", action='store_true', help="start webserver")

    # Parse the arguments from the command line
    args = parser.parse_args()

    # Use the arguments in your script
    if args.web:
        runWebserver_dataFromSharedMemory(args.file)
    else:
        desc, data = readFile(args.file)
        print(desc)
        for v in data:
            print(v)
        print()
        
