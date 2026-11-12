import struct
import sys

with open('MM32G0001_16.FLM', 'rb') as f:
    data = f.read()

print(f'FLM file size: {len(data)} bytes')

e_type, e_machine, e_version = struct.unpack_from('<HHI', data, 16)
e_entry, e_phoff, e_shoff = struct.unpack_from('<III', data, 24)
e_flags = struct.unpack_from('<I', data, 36)[0]
e_ehsize, e_phentsize, e_phnum = struct.unpack_from('<HHH', data, 40)
e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', data, 46)

print(f'ELF: type={e_type}, machine={e_machine}')
print(f'Entry: 0x{e_entry:08X}')
print(f'Program headers: offset=0x{e_phoff:X}, num={e_phnum}, entsize={e_phentsize}')
print(f'Section headers: offset=0x{e_shoff:X}, num={e_shnum}, entsize={e_shentsize}, strndx={e_shstrndx}')

# Parse program headers
print('\n=== Program Headers ===')
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    p_type, p_offset, p_vaddr, p_paddr = struct.unpack_from('<IIII', data, off)
    p_filesz, p_memsz, p_flags, p_align = struct.unpack_from('<IIII', data, off + 16)
    print(f'  PH{i}: type=0x{p_type:X} offset=0x{p_offset:X} vaddr=0x{p_vaddr:08X} paddr=0x{p_paddr:08X}')
    print(f'        filesz=0x{p_filesz:X} memsz=0x{p_memsz:X} flags=0x{p_flags:X} align=0x{p_align:X}')
    if p_type == 1:  # PT_LOAD
        print(f'        -> LOAD segment: {p_filesz} bytes at file offset 0x{p_offset:X}')
        code_start = p_offset
        print(f'        -> Code at file offset 0x{code_start:X}: {data[code_start:code_start+32].hex()}')

# Parse section headers
print('\n=== Section Headers ===')
shstrtab_offset = 0
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    sh_name, sh_type, sh_flags = struct.unpack_from('<III', data, off)
    sh_addr, sh_offset, sh_size = struct.unpack_from('<III', data, off + 12)
    sh_link, sh_info = struct.unpack_from('<II', data, off + 24)
    sh_addralign, sh_entsize = struct.unpack_from('<II', data, off + 32)

    if i == e_shstrndx:
        shstrtab_offset = sh_offset

    type_name = {0:'NULL', 1:'PROGBITS', 2:'SYMTAB', 3:'STRTAB', 8:'NOBITS'}.get(sh_type, f'{sh_type}')
    print(f'  SH{i}: name_idx={sh_name} type={type_name} addr=0x{sh_addr:08X} offset=0x{sh_offset:X} size=0x{sh_size:X}')

# Get section name string table
def get_shname(idx):
    if shstrtab_offset == 0:
        return f'[idx={idx}]'
    end = data.find(b'\x00', shstrtab_offset + idx)
    if end == -1:
        return f'[idx={idx}]'
    return data[shstrtab_offset + idx:end].decode('ascii', errors='replace')

# Parse symbol table
print('\n=== Symbol Table ===')
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    sh_type = struct.unpack_from('<I', data, off + 4)[0]
    if sh_type == 2:  # SYMTAB
        sh_name, _, _ = struct.unpack_from('<III', data, off)
        sh_addr, sh_offset, sh_size = struct.unpack_from('<III', data, off + 12)
        sh_link, sh_info = struct.unpack_from('<II', data, off + 24)
        sh_entsize = struct.unpack_from('<I', data, off + 36)[0]

        name = get_shname(sh_name)
        print(f'  Section name: {name}, entsize={sh_entsize}, num={sh_size // sh_entsize}')

        strtab_off = 0
        for j in range(e_shnum):
            off2 = e_shoff + j * e_shentsize
            if j == sh_link:
                strtab_off = struct.unpack_from('<I', data, off2 + 16)[0]

        for j in range(sh_size // sh_entsize):
            sym_off = sh_offset + j * sh_entsize
            st_name, st_value, st_size = struct.unpack_from('<III', data, sym_off)
            st_info, st_other, st_shndx = struct.unpack_from('<BBH', data, sym_off + 12)

            if st_name > 0:
                end = data.find(b'\x00', strtab_off + st_name)
                sym_name = data[strtab_off + st_name:end].decode('ascii', errors='replace')
                if sym_name in ['Init', 'UnInit', 'EraseChip', 'EraseSector', 'ProgramPage']:
                    print(f'  {sym_name}: value=0x{st_value:08X} size={st_size} shndx={st_shndx}')

print('\n=== FlashDevice struct search ===')
# Search for FlashDevice pattern in the file
# FlashDevice starts with version (2 bytes) then name (128 bytes)
# Look for string "MM32G0001" which we know is at offset around 0x23C
for marker in [b'MM32G0001', b'Flash', b'\x01\x01']:
    idx = data.find(marker)
    while idx != -1:
        print(f'  Found {marker} at file offset 0x{idx:X}')
        idx = data.find(marker, idx + 1)

# Look at FlashDevice structure around the MM32G0001 string
print('\n=== FlashDevice around MM32G0001 ===')
idx = data.find(b'MM32G0001')
if idx >= 0:
    # FlashDevice starts before the name
    # struct FlashDevice { uint16_t Vers; char DevName[128]; uint16_t DevType; ... }
    fd_start = idx - 2  # Vers is 2 bytes before DevName
    print(f'FlashDevice start (estimated): 0x{fd_start:X}')
    print(f'Vers: 0x{struct.unpack_from("<H", data, fd_start)[0]:04X}')
    # Name: 128 bytes starting at fd_start+2
    name_bytes = data[fd_start+2:fd_start+130]
    null_pos = name_bytes.find(b'\x00')
    if null_pos >= 0:
        print(f'DevName: {name_bytes[:null_pos].decode("ascii")}')
    # DevType: 2 bytes at fd_start+130
    dev_type = struct.unpack_from('<H', data, fd_start + 130)[0]
    print(f'DevType: {dev_type}')
    # DevAdr: 4 bytes at fd_start+132
    dev_adr = struct.unpack_from('<I', data, fd_start + 132)[0]
    print(f'DevAdr: 0x{dev_adr:08X}')
    # szDev: 4 bytes at fd_start+136
    sz_dev = struct.unpack_from('<I', data, fd_start + 136)[0]
    print(f'szDev: 0x{sz_dev:08X} ({sz_dev} bytes, {sz_dev//1024}KB)')
    # szPage: 4 bytes at fd_start+140
    sz_page = struct.unpack_from('<I', data, fd_start + 140)[0]
    print(f'szPage: 0x{sz_page:08X} ({sz_page} bytes)')
    # Reserved: 4 bytes at fd_start+144
    reserved = struct.unpack_from('<I', data, fd_start + 144)[0]
    print(f'Reserved: 0x{reserved:08X}')
    # From offset 148 onwards depends on version
    # For newer versions: valAdr(4), valInit(1), szSector(2), ...
    # Let's print raw bytes after reserved
    print(f'Raw after reserved: {data[fd_start+148:fd_start+180].hex()}')
