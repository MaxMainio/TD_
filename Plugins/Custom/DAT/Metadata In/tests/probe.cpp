#include "Metadata.h"
#include <filesystem>
#include <iostream>
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    Metadata::Request request;
    request.resources = std::filesystem::absolute(argv[1]).string();
    request.path = std::filesystem::absolute(argv[2]).string();
    request.deep = argc > 3;
    Metadata::Context context{[] { return false; }, Metadata::Clock::now() + std::chrono::seconds(30)};
    try {
        auto result = Metadata::extract(request, context);
        Metadata::JSON out = {{"status",result.status},{"error",result.error},{"warnings",result.warnings},{"rows",Metadata::JSON::array()}};
        for (const auto& row : result.rows) out["rows"].push_back({{"key",row.key},{"value",row.value},{"group",row.group},{"source",row.source}});
        std::cout << out.dump(2) << '\n';
        return result.error.empty() ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
