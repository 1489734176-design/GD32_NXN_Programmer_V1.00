1、烧录算法转换工具是flash_algo.exe，烧录算法工具利用.FLM文件与c_blob.tmpl提取信息为c文件。
2、.FLM来源于Keil芯片扩展包安装目录下存放的文件。Keil安装了那些芯片扩展包就会存在那些芯片的烧录算法。Keil一般默认芯片扩展包安装目录C:\Users\用户名\AppData\Local\Arm\Packs\……。在扩展包安装目录下直接搜索.FLM。比如我的GD32F103扩展包烧录算法目录：C:\Users\LIU\AppData\Local\Arm\Packs\GigaDevice\GD32F10x_DFP\2.0.2\Flash。
3、先使用flash_algo工具把烧录算法转换为C文件，然后手动填写sizeof(flash_addr)、sizeof(flash_size)、sizeof(flash_code)字段，再利用C2B转换助手工具把C文件转换为bin文件。存放到离线烧录器相应目录下既可以使用。
4、c_blob.tmpl文件烧录算法转换格式定义如下：
const program_target_t flash_algo = {
    {{'0x%08X' % (algo.symbols['Init'] + header_size + entry)}},  	// 烧录算法初始化地址
    {{'0x%08X' % (algo.symbols['UnInit'] + header_size + entry)}},  	// 烧录算法未初始化地址
    {{'0x%08X' % (algo.symbols['EraseChip'] + header_size + entry)}},  	// 烧录算法片擦除地址
    {{'0x%08X' % (algo.symbols['EraseSector'] + header_size + entry)}},  // 烧录算法扇区擦除地址
    {{'0x%08X' % (algo.symbols['ProgramPage'] + header_size + entry)}},  // 烧录算法编程地址
    {{'0x%08X' % (entry + 1)}},
    {{'0x%08X' % (entry + 1024*3)}},
    {{'0x%08X' % stack_pointer}},
    {{'0x%08X' % (entry + 1024)}},  // mem buffer location
    {{'0x%08X' % entry}},  // location to write prog_blob in target RAM
    {{'0x%08X' % algo.flash_page_size}},  // ram_to_flash_bytes_to_be_written
    sizeof(flash_addr),  // 目标芯片Flash地址，手动修改，4字节对齐
    sizeof(flash_size),  // 目标芯片Flash大小，手动修改，4字节对齐
    sizeof(flash_code),  // 烧录算法大小，手动修改，4字节对齐
    {{prog_header}} // 烧录算法
    {{algo.format_algo_data(4, 8, "c")}},  
};
5、烧录算法转换后的格式与程序里定义结构体格式一致，程序从文件系统读取烧录算法bin文件后直接使用。
typedef struct {
    uint32_t breakpoint;
    uint32_t static_base;
    uint32_t stack_pointer;
} program_syscall_t;

typedef struct {
    uint32_t  init;
    uint32_t  uninit;
    uint32_t  erase_chip;
    uint32_t  erase_sector;
    uint32_t  program_page;
    program_syscall_t sys_call_s;
    uint32_t  program_buffer;
    uint32_t  algo_start;
    uint32_t  program_buffer_size;
    uint32_t  flash_addr;
    uint32_t  flash_size;
    uint32_t  algo_size;
    uint32_t  algo_blob[1024];
} program_target_t;