import csv
import random

symbols = ["AAPL", "TSLA", "GOOG", "AMZN", "MSFT"]

def generate_tick_data(rows):
    data = []
    price_map = {s: 100.0 for s in symbols}

    for _ in range(rows):
        symbol = random.choice(symbols)

        num_pairs = random.randint(1, 4)  # variable columns
        row = [symbol]

        for _ in range(num_pairs):
            price_map[symbol] += random.uniform(-2, 2)
            price = round(price_map[symbol], 2)
            volume = random.randint(10, 1000)

            row.extend([price, volume])

        data.append(row)

    return data

for i in range(1, 5):
    with open(f"tick_data_{i}.csv", "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerows(generate_tick_data(2000))