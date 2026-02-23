// Flags for wmap
#define MAP_SHARED 0x0002
#define MAP_ANONYMOUS 0x0004
#define MAP_FIXED 0x0008

// When any system call fails, returns -1
#define FAILED -1
#define SUCCESS 0

// for `getwmapinfo`
#define MAX_WMMAP_INFO 16
struct wmapinfo {
    uint64 total_mmaps;                    // Total number of wmap regions
    void* addr[MAX_WMMAP_INFO];           // Starting address of mapping
    uint64 length[MAX_WMMAP_INFO];         // Size of mapping
    uint64 n_loaded_pages[MAX_WMMAP_INFO]; // Number of pages physically loaded into memory
};
