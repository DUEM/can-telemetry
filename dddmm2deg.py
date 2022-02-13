# Converts DDDMM.MMMM coordinates to decimal degrees

lat = 5106.8623046875
lon = 32.83409881591797

def convert(value, direction):
    DD = value//100
    MM = value - 100*DD
    degrees = DD + MM/60
    if direction == "W" or direction == "S":
        degrees = -degrees
    return degrees

lat = convert(lat,"N")
lon = convert(lon,"W")

print("Coordinates: (%f,%f)\n" % (lat, lon))