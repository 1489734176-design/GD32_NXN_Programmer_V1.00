import struct

with open('MM32G0001_16.FLM', 'rb') as f:
    data = f.read()

# Parse ELF header
e_shoff = struct.unpack_from('<I', data, 32)[0]
e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', data, 46)

# Get section name string table
off = e_shoff + e_shstrndx * e_shentsize
_, _, _ = struct.unpack_from('<III', data, off)
shstrtab_offset = struct.unpack_from('<I', data, off + 16)[0]

def get_shname(idx):
    end = data.find(b'\x00', shstrtab_offset + idx)
    return data[shstrtab_offset + idx:end].decode('ascii', errors='replace')

# Show all section names
print('=== All Section Names ===')
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    sh_name = struct.unpack_from('<I', data, off)[0]
    name = get_shname(sh_name)
    sh_addr, sh_offset, sh_size = struct.unpack_from('<III', data, off + 12)
    print(f'  SH{i}: {name} addr=0x{sh_addr:08X} offset=0x{sh_offset:X} size=0x{sh_size:X}')

# Parse ALL symbols
print('\n=== All Symbols ===')
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    sh_type = struct.unpack_from('<I', data, off + 4)[0]
    if sh_type == 2:  # SYMTAB
        sh_offset, sh_size = struct.unpack_from('<II', data, off + 16)
        sh_link = struct.unpack_from('<I', data, off + 24)[0]
        sh_entsize = struct.unpack_from('<I', data, off + 36)[0]

        # Get string table offset
        strtab_off = 0
        for j in range(e_shnum):
            off2 = e_shoff + j * e_shentsize
            if j == sh_link:
                strtab_off = struct.unpack_from('<I', data, off2 + 16)[0]

        for j in range(sh_size // sh_entsize):
            sym_off = sh_offset + j * sh_entsize
            st_name, st_value, st_size = struct.unpack_from('<III', data, sym_off)
            st_info, st_other, st_shndx = struct.unpack_from('<BBH', data, sym_off + 12)

            sym_name = ""
            if st_name > 0:
                end = data.find(b'\x00', strtab_off + st_name)
                sym_name = data[strtab_off + st_name:end].decode('ascii', errors='replace')
            if sym_name:
                bind = (st_info >> 4) & 0xF
                typ = st_info & 0xF
                print(f'  {sym_name}: value=0x{st_value:08X} size={st_size} shndx={st_shndx} bind={bind} type={typ}')
