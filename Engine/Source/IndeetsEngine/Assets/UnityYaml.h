#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ie::UnityYaml {

// One object of a Unity serialized file ("--- !u!<classId> &<fileId>").
struct Document {
    int classId = 0;
    int64_t fileId = 0;
    bool stripped = false;  // placeholder for an object that lives in a prefab instance's source
    std::string type;       // "GameObject", "Transform", "MonoBehaviour", ...
    nlohmann::json body;    // the object's fields
};

// Parses the YAML subset Unity writes (.unity, .prefab, .asset, .mat, .meta ...).
// Scalars are kept as strings (Unity's YAML is untyped); maps and sequences become JSON
// objects and arrays. Files without "--- !u!" headers (.meta) come back as one document.
std::vector<Document> Parse(const std::string& text);
std::vector<Document> ParseFile(const std::filesystem::path& path);

// Helpers for reading parsed values.
double Number(const nlohmann::json& value, double fallback = 0.0);
int64_t Integer(const nlohmann::json& value, int64_t fallback = 0);
std::string String(const nlohmann::json& value);

} // namespace ie::UnityYaml
