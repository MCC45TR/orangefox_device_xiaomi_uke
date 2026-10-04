// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "uke.h"

namespace ure {
// Listing/frontier working memory is separate from bounded JSON pages and
// hardlink indexes. Anonymous scratch files belong to the reviewed backup store.
class TreeListingBudget {
public:
    static constexpr std::size_t memory_limit=2*1024*1024;
    static constexpr std::uint64_t scratch_limit=512ULL*1024*1024;
    static constexpr unsigned entry_limit=1000000, directory_limit=100000;
    std::size_t memory=0, peak_memory=0;
    std::uint64_t scratch=0, peak_scratch=0;
    unsigned admitted_entries=1;
    void acquire_memory(std::size_t bytes);
    void acquire_scratch(std::uint64_t bytes);
    void admit_child();
};
class TreeListingMemory {
    TreeListingBudget* budget_;
    std::size_t bytes_;
public:
    TreeListingMemory(TreeListingBudget& budget,std::size_t bytes);
    ~TreeListingMemory();
    TreeListingMemory(const TreeListingMemory&)=delete;
    TreeListingMemory& operator=(const TreeListingMemory&)=delete;
};
class SortedTreeDirectory {
    TreeListingBudget* budget_=nullptr;
    Fd file_;
    std::uint64_t bytes_=0, cursor_=0;
    void append(std::string_view bytes);
public:
    SortedTreeDirectory()=default;
    SortedTreeDirectory(int directory,int scratch_directory,TreeListingBudget& budget,bool admit_children=false);
    ~SortedTreeDirectory();
    SortedTreeDirectory(SortedTreeDirectory&& other) noexcept;
    SortedTreeDirectory& operator=(SortedTreeDirectory&& other) noexcept;
    SortedTreeDirectory(const SortedTreeDirectory&)=delete;
    SortedTreeDirectory& operator=(const SortedTreeDirectory&)=delete;
    void rewind() noexcept { cursor_=0; }
    bool next(std::string& name);
};
} // namespace ure
