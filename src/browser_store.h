#pragma once

#include <string>

namespace glassglide {

class BrowserStore {
public:
    enum class LoadResult { Loaded, Missing, Error };

    BrowserStore() = default;
    ~BrowserStore();
    BrowserStore(const BrowserStore&) = delete;
    BrowserStore& operator=(const BrowserStore&) = delete;

    bool open(const std::string& directory, std::string& error);
    LoadResult load(std::string& data, std::string& error) const;
    bool save(const std::string& data, std::string& error);

private:
    int directory_ = -1;
    unsigned counter_ = 0;
};

} // namespace glassglide
