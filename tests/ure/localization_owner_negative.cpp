// Host-only ASan oracle reproducing the former C++20 JsonCpp owner lifetime.
// Deliberately invalid. Never registered as a passing CTest or tablet payload.
#include <json/json.h>
#include <iostream>
Json::Value old_catalog() {
    Json::Value owner; owner["strings"] = Json::arrayValue;
    Json::Value row; row["source"] = "Confirm"; row["name"] = "ure_confirm";
    owner["strings"].append(row); return owner;
}
int main() {
    for (const auto& row : old_catalog()["strings"]) std::cout << row["source"].asString() << '\n';
}
