# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Read the selected uncompressed TensorFlow V2 tensor-bundle checkpoint.

Project-owned, narrow reader of the public SSTable/protobuf wire formats. It
rejects unsupported compression, dtypes, shards and shapes rather than guessing.
No TensorFlow, protobuf, Snappy or historical training dependencies are needed.
"""
from pathlib import Path
import struct
import numpy as np


def masked_crc32c(data):
    table=[]
    for index in range(256):
        value=index
        for _ in range(8):
            value=(value>>1)^ (0x82f63b78 if value&1 else 0)
        table.append(value)
    crc=0xffffffff
    for byte in data:
        crc=table[(crc^byte)&255]^(crc>>8)
    crc^=0xffffffff
    return (((crc>>15)|(crc<<17))+0xa282ead8)&0xffffffff


def varint(data, pos=0):
    value = shift = 0
    while True:
        byte = data[pos]; pos += 1
        value |= (byte & 127) << shift
        if byte < 128:
            return value, pos
        shift += 7
        if shift > 63:
            raise ValueError("Invalid varint")


def fields(data):
    pos = 0
    result = {}
    while pos < len(data):
        tag, pos = varint(data, pos)
        number, wire = tag >> 3, tag & 7
        if wire == 0:
            value, pos = varint(data, pos)
        elif wire == 2:
            size, pos = varint(data, pos)
            value = data[pos:pos+size]; pos += size
        elif wire in (1, 5):
            size = 8 if wire == 1 else 4
            value = data[pos:pos+size]; pos += size
        else:
            raise ValueError(f"Unsupported protobuf wire type {wire}")
        result.setdefault(number, []).append(value)
    return result


def block_entries(data):
    restart_count = struct.unpack_from('<I', data, len(data)-4)[0]
    limit = len(data) - 4 * (restart_count + 1)
    pos = 0
    key = b''
    while pos < limit:
        shared, pos = varint(data, pos)
        unshared, pos = varint(data, pos)
        size, pos = varint(data, pos)
        key = key[:shared] + data[pos:pos+unshared]; pos += unshared
        value = data[pos:pos+size]; pos += size
        yield key, value


def read_checkpoint(prefix):
    prefix = Path(prefix)
    index = prefix.with_suffix('.index').read_bytes()
    if index[-8:] != bytes.fromhex('57fb808b247547db'):
        raise ValueError('Not a TensorFlow SSTable checkpoint index')
    _, pos = varint(index, len(index)-48)
    _, pos = varint(index, pos)
    index_offset, pos = varint(index, pos)
    index_size, pos = varint(index, pos)
    def block(offset, size):
        if index[offset+size] != 0:
            raise ValueError('Compressed checkpoint index is unsupported')
        if masked_crc32c(index[offset:offset+size+1]) != struct.unpack_from('<I',index,offset+size+1)[0]:
            raise ValueError('Checkpoint index block checksum mismatch')
        return index[offset:offset+size]
    shard = prefix.with_suffix('.data-00000-of-00001').read_bytes()
    output = {}
    for _, handle in block_entries(block(index_offset, index_size)):
        offset, pos = varint(handle)
        size, pos = varint(handle, pos)
        for key, value in block_entries(block(offset, size)):
            if not key:
                continue
            entry = fields(value)
            dtype = entry[1][0]
            if entry.get(3, [0])[0] != 0:
                raise ValueError('Only single-shard checkpoints are supported')
            dims = [fields(dim)[1][0] for dim in fields(entry[2][0]).get(2, [])]
            offset = entry.get(4, [0])[0]; size = entry[5][0]
            kind = {1: '<f4', 3: '<i4'}.get(dtype)
            if kind is None:
                raise ValueError(f'Unsupported tensor dtype {dtype}')
            content=shard[offset:offset+size]
            if masked_crc32c(content) != struct.unpack('<I',entry[6][0])[0]:
                raise ValueError(f'Checkpoint tensor checksum mismatch: {key!r}')
            array = np.frombuffer(content, dtype=kind).copy().reshape(dims)
            output[key.decode()] = array
    return output


if __name__ == '__main__':
    for name, array in read_checkpoint(Path(__file__).parent/'original/checkpoints/model-17900').items():
        print(name, array.shape, array.dtype)
