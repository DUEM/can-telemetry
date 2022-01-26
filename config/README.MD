# CAN Configuration File
CAN messages are described by the [recv_conf.json](recv_conf.json) file as in the following example:

```json
"0xFA": [
    {
        "name": "Angle and sats",
        "source": "TelemetryGPS",
        "structure": ["float32", "uint8"],
        "fields": ["angle", "sats"],
        "labels": ["Angle", "No. of sats"]
    }
]
```

The `structure` describes the type and size of data. This directly corresponds to the `fields` and `labels`. See `types` in [telemfunctions.py](../src/telemfunctions.py) for a list of current datatypes available.

> **NB:** Currently, any text in the `structure` field following a underscore will be ignored. This is redundant but has been kept to provide a comment function e.g. can be used as a reminder that the data contained nested data (`_ND`). The presence of nested data is actually defined from `fields` (see below). 

In this example the CAN message `0xFA` contains the angle of travel and the number of satellites, both as reported by the GPS onboard the telemetry system. These data are stored in `angle` and `sats` respectively, with data type `float32` and `uint8` respectively.

**Important:** if you change the datatype of what is being written to InfluxDB, InfluxDB may give an error as it is expecting data of a certain type. Either create a name field name or delete the last data.

## Arbitration ID
Given the CAN ID is an 11-bit field, it can be expressed with either 2 or 3 hexadecimal characters. Ensure there are **no leading zeros** ie. use `0xAB` not `0x0AB`.

## Nested data
Some CAN messages are more irritating and the information they carry does not fit into nice whole bytes. By placing `_Y` after the variable name (in `fields`), we can specify that the field contains nested data.

If the MSB of the field `sats` were a flag, it could be described by using `sats_Y`. The additional field `nested_data` describes the position and names of any flags.

```json
"nested_data" : {
    "sats_Y": {
        "nd_structure": ["b1","X7"],
        "nd_fields": ["a_flag"],
        "nd_labels": ["Sample Flag"]
    }
}
```

>**NB:** the actual `sats` field does not contain nested data, this is just an example.

`nd_structure` describes the size of the field (must be a multiple of bytes) and position of data:

| Value | Datatype | Above example |
| --- | --- | --- |
| `b1` | A single bit | `b1` maps the bit in the 1st position to `a_flag` |
| `X`n | *Don't care* for the next `n` bits (`n`<10) | `X7` skips the remaining 7 bits; these may hold the value of byte `sats` or don't carry useful information |

Other routines could be added as needed eg `int3` could describe a nested integer of 3 bits. However, for the residual data eg the 7 bits integer `sats` as above, it's be better to use the main process as follows.

### Multi-datatype fields
The nested data does not remove the bits it has accessed from the data. If the parent byte contains other data which will be extracted by an unpack byte(s) function, the nested data will have to be ignored when unpacking. This could be achieved using a [mask](https://en.wikipedia.org/wiki/Mask_(computing)#Masking_bits_to_0) in a custom unpacking function.

In the example above, the first bit must be ignored when unpacking. The `up_uint8` function can be modified to `up_uint8seven`:

```python
def up_uint8seven(data, offset):
    masked = data[offset:offset+1] & 0b01111111
    [x] = struct.unpack('>B', masked)
    offset += 1
    return x, offset
```

The second line *ANDs* the data and the mask to remove the first bit.

## Multipliers
Some data needs to multiplied by a constant in order to get data in the desired unit. For example, the GPS unit gives speed in knots. Multiplication by `1.852` to get speed in kmh is specified in the appropriate position in the optional `multiplier` field. The final units are specified in the optional `units` field. This is not currently used by the program but is a useful reminder of the units.

```json
"0xF7": [
    {
        "name": "Speed and angle",
        "source": "GPS",
        "structure": ["float32","float32"],
        "fields": ["speed","angle"],
        "labels": ["Speed","Angle"],
        "units": ["kmh", "Deg"],
        "multiplier": [1.852, 1]
    }
]
```

Use of the multiplier can be disabled with `useMultiplier` at the beginning of [telemfunctions.py](../src/telemfunctions.py).