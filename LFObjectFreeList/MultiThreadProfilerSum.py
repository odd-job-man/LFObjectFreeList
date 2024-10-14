# -*- coding: utf-8 -*-
import re
from collections import defaultdict

data = []
print("Paste your data here and press Enter : ")

while True:
    try:
       line = input()
       if line.strip():  # 빈 줄이 아닐 경우에만 추가
            data.append(line)
       else:
            break  # 빈 줄이면 종료
    except EOFError:  # Ctrl+D 또는 Ctrl+Z
        break

# 모든 데이터 문자열로 결합
data = "\n".join(data)

# Initialize dictionary to store aggregated data
aggregated_data = defaultdict(lambda: {"TotalTime": 0, "Correction": 0, "min": float('inf'), "max": float('-inf'), "call": 0})

# Parse the data
for line in data.splitlines():
    match = re.search(r'\|\s+(.*?)\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+[\d.]+\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|', line)
    if match:
        name, total_time, correction, min_ns, max_ns, call_count = match.groups()
        total_time, correction, min_ns, max_ns, call_count = int(total_time), int(correction), int(min_ns), int(max_ns), int(call_count)

        # Aggregate the data
        aggregated_data[name]["TotalTime"] += total_time
        aggregated_data[name]["Correction"] += correction
        aggregated_data[name]["min"] = min(aggregated_data[name]["min"], min_ns)
        aggregated_data[name]["max"] = max(aggregated_data[name]["max"], max_ns)
        aggregated_data[name]["call"] += call_count

# Print aggregated results with calculated averages
header = "| {:<100} | {:>15} | {:>15} | {:>15} | {:>15} | {:>15} | {:>10} |".format(
    "Name", "TotalTime(ns)", "Correction(ns)", "Average (ns)", "Min (ns)", "Max (ns)", "Call"
)
separator = "-" * len(header)

print(separator)
print(header)
print(separator)
for name, stats in aggregated_data.items():
    avg_ns = stats["Correction"] / stats["call"] if stats["call"] > 0 else 0
    row = "| {:<100} | {:>15} | {:>15} | {:>15.6f} | {:>15} | {:>15} | {:>10} |".format(
        name, stats["TotalTime"], stats["Correction"], avg_ns, stats["min"], stats["max"], stats["call"]
    )
    print(row)
print(separator)

input();

