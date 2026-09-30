from PIL import Image

# 10x10 1-bit icon: three stacked slabs, shrinking towards the top
img = Image.new("1", (10, 10), 0)
px = img.load()
for (x0, x1, y0, y1) in [(0, 9, 7, 9), (1, 8, 4, 6), (3, 6, 1, 3)]:
    for x in range(x0, x1 + 1):
        for y in range(y0, y1 + 1):
            px[x, y] = 1
img.save("icon.png")
print("icon.png written", img.size, img.mode)
