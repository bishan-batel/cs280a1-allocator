

expected: [str] = open("expected.txt", "r").readlines()


i = 0

file = None


for line in expected:
    if line.startswith("==="):
        i = i + 1
        if file:
            file.flush()
            file.close()
        file = open(f"tests/expected_{i}.txt", "w")

    file.write(line)
