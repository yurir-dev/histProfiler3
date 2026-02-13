import argparse
import struct
import pdb
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
    _maxSample, _minSample, _overfows, _sum, _numSamples, _samplesPerBucket, _labelLen  = struct.unpack("<7Q", header_data)

    # label
    label_data = f.read(256)
    label = label_data[:_labelLen].decode('utf-8')

    # buckets
    header_data = f.read(8)
    bucketLen = struct.unpack("<1Q", header_data)[0]
    buckets_data = f.read(bucketLen * 8)
    buckets = struct.unpack(f"<{bucketLen}Q", buckets_data)

    desc = f"{label}\n"
    desc += f"#buckets: {bucketLen}, #samples: {_numSamples}, #samples: {_overfows}, ns/bucket: {_samplesPerBucket}\n"

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

def runWebserver(fileName):
    # HTML and JavaScript content
    HTML_TEMPLATE = """
<!DOCTYPE html>
<html>
<head>
    <title>Dynamic Data Graph</title>
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
    def generate_data(fileName):
        desc, data = readFile(fileName)
        return {
            "labels": [i for i in range(0, len(data))],
            "values": list(data),
            "description": desc
        }
    
        return {
            "labels": ["Jan", "Feb", "Mar", "Apr", "May"],
            "values": [10, 25, 15, 30, 20]
        }

    app = Flask(__name__)

    @app.route('/')
    def index():
        # Serving a minimal HTML structure; JS will do the heavy lifting
        return render_template_string(HTML_TEMPLATE)

    @app.route('/api/data')
    def get_data():
        return jsonify(generate_data(fileName))

    
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
        runWebserver(args.file)
    else:
        desc, data = readFile(args.file)
        print(desc)
        for v in data:
            print(v)
        print()
        
