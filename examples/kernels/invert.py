def process(width, height, rgba):
    out = bytearray(rgba)
    for i in range(0, width * height * 4, 4):
        out[i] = 255 - out[i]
        out[i + 1] = 255 - out[i + 1]
        out[i + 2] = 255 - out[i + 2]
    return out
