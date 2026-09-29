// SPDX-License-Identifier: MIT
// Read-only inventory of Qualcomm rawprogram XML. Never opens a block device.
#include <libxml/parser.h>
#include <libxml/tree.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {
struct Program {
    unsigned int lun{};
    std::string label;
    std::string filename;
    std::string start_expression;
    std::uint64_t start{};
    std::uint64_t sectors{};
    std::uint64_t bytes_per_sector{};
    bool start_resolved{};
};

std::string property(xmlNodePtr node, const char* name) {
    std::unique_ptr<xmlChar, decltype(xmlFree)> value(xmlGetProp(node, BAD_CAST name), xmlFree);
    if (!value) throw std::runtime_error(std::string("Missing XML attribute: ") + name);
    std::string text(reinterpret_cast<const char*>(value.get()));
    if (text.find_first_of("\t\r\n") != std::string::npos || text.size() > 256)
        throw std::runtime_error(std::string("Invalid XML attribute: ") + name);
    return text;
}

std::uint64_t number(std::string_view text, const char* name) {
    std::uint64_t result{};
    const auto [next, error] = std::from_chars(text.data(), text.data() + text.size(), result, 10);
    if (error != std::errc{} || next != text.data() + text.size())
        throw std::runtime_error(std::string("Invalid decimal ") + name);
    return result;
}

bool checked_add(std::uint64_t a, std::uint64_t b, std::uint64_t& result) {
    if (b > std::numeric_limits<std::uint64_t>::max() - a) return false;
    result = a + b;
    return true;
}

void collect(xmlNodePtr node, std::vector<Program>& result) {
    for (xmlNodePtr child = node; child != nullptr; child = child->next) {
        if (child->type == XML_ELEMENT_NODE && xmlStrEqual(child->name, BAD_CAST "program")) {
            Program item;
            const auto lun = number(property(child, "physical_partition_number"), "physical_partition_number");
            if (lun > 255) throw std::runtime_error("LUN is out of range");
            item.lun = static_cast<unsigned int>(lun);
            item.label = property(child, "label");
            item.filename = property(child, "filename");
            item.start_expression = property(child, "start_sector");
            item.sectors = number(property(child, "num_partition_sectors"), "num_partition_sectors");
            item.bytes_per_sector = number(property(child, "SECTOR_SIZE_IN_BYTES"), "SECTOR_SIZE_IN_BYTES");
            if (item.bytes_per_sector != 512 && item.bytes_per_sector != 4096)
                throw std::runtime_error("Unexpected logical sector size");
            if (item.sectors > std::numeric_limits<std::uint64_t>::max() / item.bytes_per_sector)
                throw std::runtime_error("Partition byte count overflows");
            // Qualcomm backup GPT expressions depend on physical disk capacity.
            // Retain them unresolved; never guess the final sector.
            if (item.start_expression.find("NUM_DISK_SECTORS") == std::string::npos) {
                item.start = number(item.start_expression, "start_sector");
                std::uint64_t end{};
                if (!checked_add(item.start, item.sectors, end))
                    throw std::runtime_error("Partition end sector overflows");
                item.start_resolved = true;
            }
            result.push_back(std::move(item));
        }
        if (child->children != nullptr) collect(child->children, result);
    }
}

void read_file(const std::filesystem::path& path, std::vector<Program>& result) {
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 16 * 1024 * 1024)
        throw std::runtime_error("Input must be a regular XML file of at most 16 MiB");
    std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)> document(
        xmlReadFile(path.c_str(), nullptr, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING), xmlFreeDoc);
    if (!document) throw std::runtime_error("Cannot parse rawprogram XML");
    xmlNodePtr root = xmlDocGetRootElement(document.get());
    if (!root || !xmlStrEqual(root->name, BAD_CAST "data"))
        throw std::runtime_error("Expected a Qualcomm rawprogram data root");
    collect(root->children, result);
}

void check_overlaps(const std::vector<Program>& entries) {
    std::vector<const Program*> sorted;
    for (const auto& entry : entries)
        if (entry.start_resolved && entry.sectors > 0) sorted.push_back(&entry);
    std::sort(sorted.begin(), sorted.end(), [](const Program* lhs, const Program* rhs) {
        return std::tie(lhs->lun, lhs->start) < std::tie(rhs->lun, rhs->start);
    });
    for (std::size_t i = 1; i < sorted.size(); ++i) {
        const auto* before = sorted[i - 1];
        const auto* after = sorted[i];
        if (before->lun == after->lun && before->start + before->sectors > after->start)
            throw std::runtime_error("Overlapping rawprogram ranges in LUN " + std::to_string(after->lun)
                + ": " + before->label + " and " + after->label);
    }
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: uke-partition-inventory RAWPROGRAM.xml [...]\n";
        return 2;
    }
    xmlInitParser();
    try {
        std::vector<Program> entries;
        for (int i = 1; i < argc; ++i) read_file(argv[i], entries);
        check_overlaps(entries);
        std::cout << "lun\tlabel\tstart_sector\tnum_sectors\tsector_bytes\tfilename\tstatus\n";
        for (const auto& entry : entries) {
            std::cout << entry.lun << '\t' << entry.label << '\t'
                << (entry.start_resolved ? std::to_string(entry.start) : entry.start_expression) << '\t'
                << entry.sectors << '\t' << entry.bytes_per_sector << '\t' << entry.filename << '\t'
                << (!entry.start_resolved ? "symbolic" : entry.sectors == 0 ? "zero-length-or-open-ended" : "resolved") << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "Inventory failed: " << error.what() << '\n';
        xmlCleanupParser();
        return 1;
    }
    xmlCleanupParser();
}
