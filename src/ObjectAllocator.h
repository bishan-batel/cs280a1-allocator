#ifndef OBJECTALLOCATORH
#define OBJECTALLOCATORH

#include <cstddef>
#include <cstdint>
#include <string>

// If the client doesn't specify these:
static constexpr int DEFAULT_OBJECTS_PER_PAGE = 4;

static constexpr int DEFAULT_MAX_PAGES = 3;

/**
 * @brief 32 Bit Floating Point Number
 */
using f32 = float;

/**
 * @brief 64 Bit Floating Point Number
 */
using f64 = long double;

/**
 * @brief Fix Sized Unsigned 8 Bit Integer (cannot be negative)
 */
using u8 = std::uint8_t;

/**
 * @brief Fix Sized Unsigned 16 Bit Integer (cannot be negative)
 */
using u16 = std::uint16_t;

/**
 * @brief Fix Sized Unsigned 32 Bit Integer (cannot be negative)
 */
using u32 = std::uint32_t;

/**
 * @brief Fix Sized Unsigned 64 Bit Integer (cannot be negative)
 */
using u64 = unsigned long int;

/**
 * @brief Biggest Unsigned Integer type that the current platform can use
 * (cannot be negative)
 */
using umax = std::uintmax_t;

/**
 * @brief Unsigned Integer for when referring to any form of memory size or
 * offset (eg. an array length or index)
 */
using usize = std::size_t;
/**
 * @brief Unsigned Integer Pointer typically used for pointer arithmetic
 */
using uptr = std::uintptr_t;

/**
 * @brief Signed 8 bit Integer
 */
using i8 = std::int8_t;

/**
 * @brief Signed 16 bit Integer
 */
using i16 = std::int16_t;

/**
 * @brief Signed 32 bit Integer
 */
using i32 = std::int32_t;

/**
 * @brief Signed 64 bit Integer
 */
using i64 = std::int64_t;

/**
 * @brief Integer pointer typically used for pointer arithmetic
 */
using iptr = std::intptr_t;

/**
  Exception class
*/
class OAException final {
public:

  /**
    Possible exception codes
  */
  enum OA_EXCEPTION {
    E_NO_MEMORY,      //!< out of physical memory (operator new fails)
    E_NO_PAGES,       //!< out of logical memory (max pages has been reached)
    E_BAD_BOUNDARY,   //!< block address is on a page, but not on any block-boundary
    E_MULTIPLE_FREE,  //!< block has already been freed
    E_CORRUPTED_BLOCK //!< block has been corrupted (pad bytes have been overwritten)
  };

  /**
   * Constructor
   * @param ErrCode One of the 5 error codes listed above
   * @param Message A message returned by the what method.
   */
  OAException(OA_EXCEPTION err_code, std::string message): error_code{err_code}, message{std::move(message)} {};

  /**
    Destructor
  */
  virtual ~OAException() {}

  /**
   * Retrieves the error code
   *
   * @return One of the 5 error codes.
   */
  inline OA_EXCEPTION code() const { return error_code; }

  /**
   * Retrieves a human-readable string regarding the error.
   *
   * @return The NUL-terminated string representing the error.
   */
  inline const char* what() const noexcept { return message.c_str(); }

private:

  OA_EXCEPTION error_code; //!< The error code (one of the 5)
  std::string message;     //!< The formatted string for the user.
};

/**
 * ObjectAllocator configuration parameters
 */
struct OAConfig final {
  static constexpr usize BASIC_HEADER_SIZE = sizeof(u32) + 1;  //!< allocation number + flags
  static constexpr usize EXTERNAL_HEADER_SIZE = sizeof(void*); //!< just a pointer

  /**
   * The different types of header blocks
   */
  enum HBLOCK_TYPE {
    hbNone,
    hbBasic,
    hbExtended,
    hbExternal
  };

  /**
   * POD that stores the information related to the header blocks.
   */
  struct HeaderBlockInfo {
    HBLOCK_TYPE type_; //!< Which of the 4 header types to use?
    usize size_;       //!< The size of this header
    usize additional_; //!< How many user-defined additional bytes

    /**
     * @brief Constructor
     *
     * @param type The kind of header blocks in use.
     * @param additional The number of user-defined additional bytes required.
     */
    HeaderBlockInfo(HBLOCK_TYPE type = hbNone, u32 additional = 0): type_(type), size_(0), additional_(additional) {
      if (type_ == hbBasic) {
        size_ = BASIC_HEADER_SIZE;
      } else if (type_ == hbExtended) { // alloc # + use counter + flag byte + user-defined
        size_ = sizeof(u32) + sizeof(u16) + sizeof(u8) + additional_;
      } else if (type_ == hbExternal) {
        size_ = EXTERNAL_HEADER_SIZE;
      }
    };
  };

  /**
   * @brief Constructor
   *
   * @param UseCPPMemManager Determines whether or not to by-pass the OA.
   *
   * @param ObjectsPerPage Number of objects for each page of memory.
   *
   * @param MaxPages Maximum number of pages before throwing an exception.
   * A value of 0 means unlimited.
   *
   * @param DebugOn Is debugging code on or off?
   *
   * @param PadBytes The number of bytes to the left and right of a block to pad with.
   *
   * @param HBInfo Information about the header blocks used
   *
   * @param Alignment The number of bytes to align on.
   *
   */
  OAConfig(
    bool UseCPPMemManager = false,
    unsigned ObjectsPerPage = DEFAULT_OBJECTS_PER_PAGE,
    unsigned MaxPages = DEFAULT_MAX_PAGES,
    bool DebugOn = false,
    unsigned PadBytes = 0,
    const HeaderBlockInfo& HBInfo = HeaderBlockInfo(),
    unsigned Alignment = 0
  ):
      UseCPPMemManager_(UseCPPMemManager),
      ObjectsPerPage_(ObjectsPerPage),
      MaxPages_(MaxPages),
      DebugOn_(DebugOn),
      PadBytes_(PadBytes),
      HBlockInfo_(HBInfo),
      Alignment_(Alignment) {
    HBlockInfo_ = HBInfo;
    LeftAlignSize_ = 0;
    InterAlignSize_ = 0;
  }

  bool UseCPPMemManager_;      //!< by-pass the functionality of the OA and use new/delete
  unsigned ObjectsPerPage_;    //!< number of objects on each page
  unsigned MaxPages_;          //!< maximum number of pages the OA can allocate (0=unlimited)
  bool DebugOn_;               //!< enable/disable debugging code (signatures, checks, etc.)
  unsigned PadBytes_;          //!< size of the left/right padding for each block
  HeaderBlockInfo HBlockInfo_; //!< size of the header for each block (0=no headers)
  unsigned Alignment_;         //!< address alignment of each block
  unsigned LeftAlignSize_;     //!< number of alignment bytes required to align first block
  unsigned InterAlignSize_;    //!< number of alignment bytes required between remaining blocks
};

/**
  POD that holds the ObjectAllocator statistical info
*/
struct OAStats final {
  /**
   * Constructor
   */
  OAStats():
      ObjectSize_(0),
      PageSize_(0),
      FreeObjects_(0),
      ObjectsInUse_(0),
      PagesInUse_(0),
      MostObjects_(0),
      Allocations_(0),
      Deallocations_(0) {};

  usize ObjectSize_;       //!< size of each object
  usize PageSize_;         //!< size of a page including all headers, padding, etc.
  unsigned FreeObjects_;   //!< number of objects on the free list
  unsigned ObjectsInUse_;  //!< number of objects in use by client
  unsigned PagesInUse_;    //!< number of pages allocated
  unsigned MostObjects_;   //!< most objects in use by client at one time
  unsigned Allocations_;   //!< total requests to allocate memory
  unsigned Deallocations_; //!< total requests to free memory
};

/**
 *This allows us to easily treat raw objects as nodes in a linked list
 */
struct GenericObject {
  GenericObject* Next; //!< The next object in the list
};

static_assert(sizeof(GenericObject) == sizeof(void*), "GenericObject has non transparent alignment/sizing");

/**
 * This is used with external headers
 */
struct MemBlockInfo {
  bool in_use;   //!< Is the block free or in use?
  char* label;   //!< A dynamically allocated NUL-terminated string
  u32 alloc_num; //!< The allocation number (count) of this block
};

/**
 * This class represents a custom memory manager
 */
class ObjectAllocator final {
public:

  // Defined by the client (pointer to a block, size of block)
  using DUMPCALLBACK = void (*)(const void*, usize);     //!< Callback function when dumping memory leaks
  using VALIDATECALLBACK = void (*)(const void*, usize); //!< Callback function when validating blocks

  // Predefined values for memory signatures

  static constexpr u8 UNALLOCATED_PATTERN = 0xAA; //!< New memory never given to the client
  static constexpr u8 ALLOCATED_PATTERN = 0xBB;   //!< Memory owned by the client
  static constexpr u8 FREED_PATTERN = 0xCC;       //!< Memory returned by the client
  static constexpr u8 PAD_PATTERN = 0xDD;         //!< Pad signature to detect buffer over/under flow
  static constexpr u8 ALIGN_PATTERN = 0xEE;       //!< For the alignment bytes

  static constexpr usize ALLOC_ID_BYTES = sizeof(u32);
  static constexpr usize USE_COUNTER_BYTES = sizeof(u16);

  static_assert(ALLOC_ID_BYTES == 4, "");
  static_assert(USE_COUNTER_BYTES == 2, "");

  /*
   * Creates the ObjectManager per the specified values
   *
   * Throws an exception if the construction fails. (Memory allocation problem)
   */
  ObjectAllocator(usize ObjectSize, const OAConfig& config);

  /*
   * Destroys the ObjectManager (never throws)
   */
  ~ObjectAllocator() noexcept;

  /*
   * Take an object from the free list and give it to the client (simulates new)
   *
   * Throws an exception if the object can't be allocated. (Memory allocation problem)
   */
  void* Allocate(const char* label = 0);

  /*
   * Returns an object to the free list for the client (simulates delete)
   *
   * Throws an exception if the the object can't be freed. (Invalid object)
   */
  void Free(void* block_void_ptr);

  /*
   * Calls the callback fn for each block still in use
   */
  u32 DumpMemoryInUse(DUMPCALLBACK callback) const;

  /*
   * Calls the callback fn for each block that is potentially corrupted
   */
  u32 ValidatePages(VALIDATECALLBACK callback) const;

  /*
   * Frees all empty pages (extra credit)
   */
  u32 FreeEmptyPages();

  /*
   * Returns true if FreeEmptyPages and alignments are implemented
   */
  static bool ImplementedExtraCredit();

  // Testing/Debugging/Statistic methods

  void SetDebugState(bool State);

  /**
   * returns a pointer to the internal free list
   * */
  const void* GetFreeList() const;

  /**
   * returns a pointer to the internal page list
   */
  const void* GetPageList() const;

  /**
   * returns the configuration parameters
   */
  const OAConfig& GetConfig() const;

  /**
   * returns the statistics for the allocator
   */
  const OAStats& GetStats() const;

  // Prevent copy construction and assignment
  ObjectAllocator(const ObjectAllocator& oa) = delete;            //!< Do not implement!
  ObjectAllocator& operator=(const ObjectAllocator& oa) = delete; //!< Do not implement!
private:

  template<typename T>
  static bool all_bytes_eq(const T* span, const usize length, const u8 byte_pattern) {
    const u8* begin = as_bytes(span);

    bool eq{true};
    for (usize i = 0; i < length; i++) {
      eq = eq && begin[i] != byte_pattern;
    }

    return eq;
  }

  auto validate_boundary(const u8* block) const -> void;

  bool is_page_empty(u8* page) const;

  void free_page(u8* page) const;

  void cull_free_blocks_in_page(const u8* page);

  /**
   * @brief Converts bytes to a generic object reference
   */
  static GenericObject& as_list(u8* bytes);

  /**
   * @brief Converts bytes to a generic object reference
   */
  static const GenericObject& as_list(const u8* bytes);

  /**
   * @brief Converts a generic object to its bytes repr
   */
  template<typename T>
  static const u8* as_bytes(const T* bytes) {
    return reinterpret_cast<const u8*>(bytes);
  }

  /**
   * @brief Converts a generic object to its bytes repr
   */
  template<typename T>
  static u8* as_bytes(T* bytes) {
    return reinterpret_cast<u8*>(bytes);
  }

  void init_header_blocks_for_page(u8* first_header) const;

  /**
   * @brief Allocates data for a new page and sets the next pointer for you (this also memsets to UNALLOCATED_PATTERn)
   */
  void allocate_page();

  bool is_in_free_list(const u8* ptr) const;

  /**
   * @brief Checks if the given page has no corrupted padding bytes
   */
  bool validate_page(const u8* page) const;

  bool validate_block(const u8* block) const;

  void setup_allocated_header(u8* header, const char* label) const;

  void setup_freed_header(u8* header) const;



  /**
   * @brief Checks if all bytes in the given span match the pattern
   */
  static bool is_signed_as(const u8* ptr, usize extents, u8 pattern);

  // Some "suggested" members (only a suggestion!)
  u8* page_list{nullptr}; //!< the beginning of the list of pages
  u8* free_list{nullptr}; //!< the beginning of the list of objects

  OAConfig config{};
  OAStats statistics{};

  usize object_size{0};

  /**
   * @brief Size of a block (without alignment)
   */
  usize block_size{0};

  usize page_size{0};

  usize block_stride{0};

  // Lots of other private stuff...
};

#endif
