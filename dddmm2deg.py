# Converts DDDMM.MMMM coordinates to decimal degrees

lat = 5106.8623046875
lon = 32.83409881591797

def convert(value):
    DD = value//100
    MM = value - 100*DD
    degrees = DD + MM/60
    return degrees

lat = convert(lat)
lon = convert(lon)

print("Coordinates: (%f,%f)\n" % (lat, lon))