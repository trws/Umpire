//////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2016-20, Lawrence Livermore National Security, LLC and Umpire
// project contributors. See the COPYRIGHT file for details.
//
// SPDX-License-Identifier: (MIT)
//////////////////////////////////////////////////////////////////////////////

#include "umpire/resource/FileMemoryResource.hpp"

#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>

namespace umpire {
namespace resource {

int FileMemoryResource::s_file_counter {0};

FileMemoryResource::FileMemoryResource(
    Platform platform, 
    const std::string& name,
    int id,
    MemoryResourceTraits traits) :
  MemoryResource(name, id, traits),
  m_platform{platform},
  m_size_map{}
{
} 

FileMemoryResource::~FileMemoryResource()
{
  std::vector<void*> leaked_items;

  for ( auto const& m : m_size_map ) {
      leaked_items.push_back(m.first);
  }

  for ( auto const& p : leaked_items ) {
      deallocate(p);
  }
}

void* FileMemoryResource::allocate(std::size_t bytes)
{
  if (bytes <= 0) { UMPIRE_ERROR( "Bytes Requested Error: Bytes size is less than 1"); }

  // Find output file directory for mmap files
  const char* memory_file_dir{std::getenv("UMPIRE_MEMORY_FILE_DIR")};
  std::string default_dir = memory_file_dir?"./":memory_file_dir;

  // Create name and open file
  std::stringstream SS;
  SS << default_dir << "umpire_mem_" << getpid() << s_file_counter;
  s_file_counter++;

  int fd{open(SS.str().c_str(), O_RDWR | O_CREAT | O_LARGEFILE, S_IRWXU)};
  if (fd == -1) { 
    UMPIRE_ERROR("Opening File Failed: " << strerror(errno)); 
  }

  // Setting Size Of Map File
  const std::size_t pagesize{ (std::size_t) sysconf(_SC_PAGE_SIZE) };
  std::size_t rounded_bytes{ ((bytes + (pagesize - 1))/ pagesize) * pagesize };

  // Truncate file
  int trun{ftruncate64(fd, rounded_bytes)};
  if (trun == -1) { 
    remove(SS.str().c_str()); UMPIRE_ERROR("Truncate Failed: " << strerror(errno)); 
  }

  // Using mmap
  void* ptr{mmap(NULL, rounded_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)};
  if (ptr == MAP_FAILED) { 
    remove(SS.str().c_str()); UMPIRE_ERROR("Mmap Failed: " << strerror(errno)); 
  }

  // Storing Information On File
  std::pair <const std::string, std::size_t> INFO{std::make_pair(SS.str(), rounded_bytes)};
  m_size_map.insert(ptr, INFO);
  
  close(fd);
  return ptr;
}

void FileMemoryResource::deallocate(void* ptr)
{
  // Find information about ptr for deallocation
  auto iter = m_size_map.find(ptr);

  // Unmap File
  if (munmap(iter->first, iter->second->second) < 0) {
      UMPIRE_ERROR("munmap failed:" << strerror(errno));
  }
  // Remove File
  if (remove(iter->second->first.c_str()) < 0) {
      UMPIRE_ERROR("remove of " << iter->second->first.c_str() << " failed: " << strerror(errno));
  }
  
  // Remove Information about file in m_size_map
  m_size_map.erase(iter->first);
}

std::size_t FileMemoryResource::getCurrentSize() const noexcept
{
  return 0;
}

std::size_t FileMemoryResource::getHighWatermark() const noexcept
{
  return 0;
}

Platform FileMemoryResource::getPlatform() noexcept
{
  return m_platform;
}

} // end of namespace resource
} // end of namespace umpire
