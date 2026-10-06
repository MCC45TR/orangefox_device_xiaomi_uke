#pragma once
#include <fixture-external.hpp>
#include <iomanip>
#define LOG(severity) ::fixture::external::Log{}
#define PLOG(severity) ::fixture::external::Log{}
#define CHECK_GT(a,b) do { if (!((a) > (b))) std::abort(); } while (false)
