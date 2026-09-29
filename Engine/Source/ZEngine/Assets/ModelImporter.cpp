#include "ZEngine/Assets/Model.h"

#include "ZEngine/Core/Log.h"
#include "ZEngine/Core/Platform.h"
#include "ZEngine/Scene/Scene.h"

#include <ufbx.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <unordered_map>

namespace ze {

namespace fs = std::filesystem;

namespace {

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// Finds texture files referenced by models (paths inside FBX files usually point to the artist's machine).
class TextureResolver {
public:
    TextureResolver(fs::path assetRoot, fs::path modelFile) : m_Root(std::move(assetRoot)), m_Model(std::move(modelFile)) {}

    std::string Resolve(const std::string& relative, const std::string& absolute)
    {
        std::vector<fs::path> names;
        for (const std::string& s : {relative, absolute})
            if (!s.empty())
                names.push_back(Platform::Utf8ToPath(s));
        fs::path dir = m_Model.parent_path();
        std::error_code ec;
        for (const fs::path& name : names) {
            for (const fs::path& candidate : {dir / name, dir / name.filename(), dir / "Textures" / name.filename(),
                                              dir.parent_path() / "Textures" / name.filename()})
                if (fs::is_regular_file(candidate, ec))
                    return ToAsset(candidate);
        }
        // Last resort: any file with the same name anywhere in Assets.
        if (!m_Indexed) {
            m_Indexed = true;
            for (auto it = fs::recursive_directory_iterator(m_Root, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
                if (!ec && it->is_regular_file(ec))
                    m_ByName.emplace(Lower(it->path().filename().string()), it->path());
        }
        for (const fs::path& name : names)
            if (auto it = m_ByName.find(Lower(name.filename().string())); it != m_ByName.end())
                return ToAsset(it->second);
        return {};
    }

    // Writes embedded image data next to the model (once) and returns its asset path.
    std::string Extract(const std::string& name, const void* data, size_t size)
    {
        if (!data || size == 0)
            return {};
        fs::path dir = m_Model.parent_path() / (m_Model.stem().string() + "_Textures");
        std::error_code ec;
        fs::create_directories(dir, ec);
        fs::path file = dir / Platform::Utf8ToPath(name).filename();
        if (!fs::exists(file, ec)) {
            std::ofstream out(file, std::ios::binary);
            out.write(static_cast<const char*>(data), std::streamsize(size));
        }
        return ToAsset(file);
    }

private:
    std::string ToAsset(const fs::path& file)
    {
        std::error_code ec;
        return Platform::PathToUtf8(fs::relative(file, m_Root, ec));
    }

    fs::path m_Root;
    fs::path m_Model;
    bool m_Indexed = false;
    std::unordered_multimap<std::string, fs::path> m_ByName;
};

void FinishPart(ModelAsset& model, ModelAsset::Part& part, const MeshData& data)
{
    part.vertexCount = uint32_t(data.vertices.size());
    part.triangleCount = uint32_t(data.indices.size() / 3);
    if (!data.vertices.empty()) {
        part.boundsMin = part.boundsMax = data.vertices[0].position;
        for (const Vertex& v : data.vertices) {
            part.boundsMin = glm::min(part.boundsMin, v.position);
            part.boundsMax = glm::max(part.boundsMax, v.position);
        }
    }
    model.totalVertices += part.vertexCount;
    model.totalTriangles += part.triangleCount;
}

glm::vec3 LinearToSRGB(glm::vec3 c)
{
    for (int i = 0; i < 3; ++i)
        c[i] = c[i] <= 0.0031308f ? c[i] * 12.92f : 1.055f * std::pow(c[i], 1.0f / 2.4f) - 0.055f;
    return c;
}

// ------------------------------------------------------------------ FBX / OBJ (ufbx)

std::unique_ptr<ModelAsset> LoadUfbx(const fs::path& file, TextureResolver& textures, std::string& error)
{
    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_left_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_X; // same mirror as Unity
    opts.generate_missing_normals = true;
    opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    opts.target_camera_axes = ufbx_axes_left_handed_y_up;
    opts.target_light_axes = ufbx_axes_left_handed_y_up;

    std::vector<char> bytes = Platform::ReadBinaryFile(file);
    if (bytes.empty()) {
        error = "cannot read file";
        return nullptr;
    }
    std::string pathString = file.string();
    opts.filename = {pathString.c_str(), pathString.size()};
    ufbx_error uerr;
    ufbx_scene* scene = ufbx_load_memory(bytes.data(), bytes.size(), &opts, &uerr);
    if (!scene) {
        char buffer[512];
        ufbx_format_error(buffer, sizeof(buffer), &uerr);
        error = buffer;
        return nullptr;
    }

    auto model = std::make_unique<ModelAsset>();
    model->format = Lower(file.extension().string()) == ".obj" ? "OBJ" : "FBX";

    // Materials.
    std::unordered_map<const ufbx_material*, int> materialIndex;
    for (size_t i = 0; i < scene->materials.count; ++i) {
        const ufbx_material* m = scene->materials.data[i];
        ModelAsset::Material mat;
        mat.name = std::string(m->name.data, m->name.length);
        ufbx_vec4 color = m->pbr.base_color.has_value ? m->pbr.base_color.value_vec4 : m->fbx.diffuse_color.value_vec4;
        if (m->pbr.base_color.has_value || m->fbx.diffuse_color.has_value)
            mat.baseColor = {float(color.x), float(color.y), float(color.z), 1.0f};
        const ufbx_texture* tex = m->pbr.base_color.texture ? m->pbr.base_color.texture : m->fbx.diffuse_color.texture;
        if (tex) {
            std::string rel(tex->relative_filename.data, tex->relative_filename.length);
            std::string abs(tex->filename.data, tex->filename.length);
            if (tex->content.size > 0)
                mat.baseColorTexture = textures.Extract(fs::path(rel.empty() ? abs : rel).filename().string(),
                                                        tex->content.data, tex->content.size);
            if (mat.baseColorTexture.empty())
                mat.baseColorTexture = textures.Resolve(rel, abs);
            if (mat.baseColorTexture.empty())
                Log::Warn("Model texture not found: {}", rel.empty() ? abs : rel);
            else
                mat.baseColor = glm::vec4(1.0f); // the texture carries the color
        }
        materialIndex[m] = int(model->materials.size());
        model->materials.push_back(std::move(mat));
    }

    // Meshes: one part per material, indexed with duplicate vertices merged.
    std::unordered_map<const ufbx_mesh*, std::vector<int>> meshParts;
    for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
        const ufbx_mesh* mesh = scene->meshes.data[mi];
        std::vector<uint32_t> triIndices(mesh->max_face_triangles * 3);
        for (size_t pi = 0; pi < mesh->material_parts.count; ++pi) {
            const ufbx_mesh_part& part = mesh->material_parts.data[pi];
            if (part.num_triangles == 0)
                continue;
            std::vector<Vertex> vertices;
            vertices.reserve(part.num_triangles * 3);
            for (size_t fi = 0; fi < part.face_indices.count; ++fi) {
                ufbx_face face = mesh->faces.data[part.face_indices.data[fi]];
                uint32_t count = ufbx_triangulate_face(triIndices.data(), triIndices.size(), mesh, face);
                for (uint32_t k = 0; k < count * 3; ++k) {
                    uint32_t index = triIndices[k];
                    Vertex v{};
                    ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
                    v.position = {float(p.x), float(p.y), float(p.z)};
                    if (mesh->vertex_normal.exists) {
                        ufbx_vec3 n = ufbx_get_vertex_vec3(&mesh->vertex_normal, index);
                        v.normal = {float(n.x), float(n.y), float(n.z)};
                    }
                    if (mesh->vertex_uv.exists) {
                        ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
                        v.uv = {float(uv.x), 1.0f - float(uv.y)}; // FBX UV origin is bottom-left
                    }
                    vertices.push_back(v);
                }
            }
            MeshData data;
            data.indices.resize(vertices.size());
            ufbx_vertex_stream stream{vertices.data(), vertices.size(), sizeof(Vertex)};
            size_t unique = ufbx_generate_indices(&stream, 1, data.indices.data(), data.indices.size(), nullptr, nullptr);
            vertices.resize(unique);
            data.vertices = std::move(vertices);

            ModelAsset::Part p;
            p.name = std::string(mesh->name.data, mesh->name.length);
            if (mesh->materials.count > pi)
                if (auto it = materialIndex.find(mesh->materials.data[pi]); it != materialIndex.end())
                    p.material = it->second;
            p.skinned = mesh->skin_deformers.count > 0;
            FinishPart(*model, p, data);
            meshParts[mesh].push_back(int(model->parts.size()));
            model->parts.push_back(std::move(p));
            model->partData.push_back(std::move(data));
        }
    }

    // Nodes (the implicit FBX root is skipped).
    std::unordered_map<const ufbx_node*, int> nodeIndex;
    for (size_t i = 0; i < scene->nodes.count; ++i) {
        const ufbx_node* n = scene->nodes.data[i];
        if (n->is_root)
            continue;
        ModelAsset::Node node;
        node.name = std::string(n->name.data, n->name.length);
        if (node.name.empty())
            node.name = n->mesh ? "Mesh" : "Node";
        const ufbx_transform& t = n->local_transform;
        node.position = {float(t.translation.x), float(t.translation.y), float(t.translation.z)};
        node.rotation = glm::quat(float(t.rotation.w), float(t.rotation.x), float(t.rotation.y), float(t.rotation.z));
        node.scale = {float(t.scale.x), float(t.scale.y), float(t.scale.z)};
        if (n->mesh)
            if (auto it = meshParts.find(n->mesh); it != meshParts.end())
                node.parts = it->second;
        nodeIndex[n] = int(model->nodes.size());
        model->nodes.push_back(std::move(node));
    }
    for (size_t i = 0; i < scene->nodes.count; ++i) {
        const ufbx_node* n = scene->nodes.data[i];
        if (!n->is_root && n->parent && !n->parent->is_root)
            model->nodes[nodeIndex[n]].parent = nodeIndex[n->parent];
    }

    for (size_t i = 0; i < scene->anim_stacks.count; ++i) {
        const ufbx_anim_stack* stack = scene->anim_stacks.data[i];
        model->animations.push_back({std::string(stack->name.data, stack->name.length),
                                     float(stack->time_end - stack->time_begin)});
    }
    ufbx_free_scene(scene);
    return model;
}

// ------------------------------------------------------------------ glTF / GLB (cgltf)

// glTF is right-handed; mirror X like Unity does.
glm::vec3 MirrorX(glm::vec3 v) { return {-v.x, v.y, v.z}; }
glm::quat MirrorX(glm::quat q) { return glm::quat(q.w, q.x, -q.y, -q.z); }

std::unique_ptr<ModelAsset> LoadGltf(const fs::path& file, TextureResolver& textures, std::string& error)
{
    std::vector<char> bytes = Platform::ReadBinaryFile(file);
    if (bytes.empty()) {
        error = "cannot read file";
        return nullptr;
    }
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse(&options, bytes.data(), bytes.size(), &data) != cgltf_result_success) {
        error = "invalid glTF";
        return nullptr;
    }
    std::string pathString = file.string();
    if (cgltf_load_buffers(&options, data, pathString.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        error = "cannot load glTF buffers (.bin files next to the model?)";
        return nullptr;
    }

    auto model = std::make_unique<ModelAsset>();
    model->format = "glTF";

    auto imagePath = [&](const cgltf_image* img, size_t index) -> std::string {
        if (!img)
            return {};
        if (img->buffer_view) {
            const cgltf_buffer_view* view = img->buffer_view;
            const char* ext = img->mime_type && std::strstr(img->mime_type, "jpeg") ? ".jpg" : ".png";
            std::string name = img->name && *img->name ? std::string(img->name) : "image_" + std::to_string(index);
            if (fs::path(name).extension().empty())
                name += ext;
            return textures.Extract(name, static_cast<const char*>(view->buffer->data) + view->offset, view->size);
        }
        if (img->uri && std::strncmp(img->uri, "data:", 5) != 0) {
            std::string uri = img->uri;
            cgltf_decode_uri(uri.data());
            uri.resize(std::strlen(uri.c_str()));
            return textures.Resolve(uri, {});
        }
        return {};
    };

    for (size_t i = 0; i < data->materials_count; ++i) {
        const cgltf_material& m = data->materials[i];
        ModelAsset::Material mat;
        mat.name = m.name ? m.name : "Material " + std::to_string(i);
        if (m.has_pbr_metallic_roughness) {
            const float* f = m.pbr_metallic_roughness.base_color_factor;
            mat.baseColor = glm::vec4(LinearToSRGB({f[0], f[1], f[2]}), f[3]);
            if (const cgltf_texture* tex = m.pbr_metallic_roughness.base_color_texture.texture)
                mat.baseColorTexture = imagePath(tex->image, size_t(tex->image - data->images));
        }
        model->materials.push_back(std::move(mat));
    }

    std::vector<std::vector<int>> meshParts(data->meshes_count);
    for (size_t mi = 0; mi < data->meshes_count; ++mi) {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (size_t pi = 0; pi < mesh.primitives_count; ++pi) {
            const cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles)
                continue;
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr;
            bool skinned = false;
            for (size_t a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute& attr = prim.attributes[a];
                if (attr.type == cgltf_attribute_type_position) pos = attr.data;
                else if (attr.type == cgltf_attribute_type_normal) nrm = attr.data;
                else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uv = attr.data;
                else if (attr.type == cgltf_attribute_type_joints) skinned = true;
            }
            if (!pos)
                continue;
            MeshData md;
            md.vertices.resize(pos->count);
            for (size_t v = 0; v < pos->count; ++v) {
                float p[3] = {}, n[3] = {0, 1, 0}, t[2] = {};
                cgltf_accessor_read_float(pos, v, p, 3);
                if (nrm) cgltf_accessor_read_float(nrm, v, n, 3);
                if (uv) cgltf_accessor_read_float(uv, v, t, 2);
                md.vertices[v] = {MirrorX({p[0], p[1], p[2]}), MirrorX({n[0], n[1], n[2]}), {t[0], t[1]}};
            }
            if (prim.indices) {
                md.indices.resize(prim.indices->count);
                for (size_t k = 0; k < prim.indices->count; ++k)
                    md.indices[k] = uint32_t(cgltf_accessor_read_index(prim.indices, k));
            } else {
                md.indices.resize(pos->count);
                for (size_t k = 0; k < pos->count; ++k)
                    md.indices[k] = uint32_t(k);
            }
            // Mirroring flips the winding: swap two corners of every triangle.
            for (size_t k = 0; k + 2 < md.indices.size(); k += 3)
                std::swap(md.indices[k + 1], md.indices[k + 2]);
            if (!nrm) {
                // Flat-ish normals from faces when the file has none.
                for (Vertex& v : md.vertices) v.normal = glm::vec3(0.0f);
                for (size_t k = 0; k + 2 < md.indices.size(); k += 3) {
                    Vertex &a = md.vertices[md.indices[k]], &b = md.vertices[md.indices[k + 1]], &c = md.vertices[md.indices[k + 2]];
                    glm::vec3 fn = glm::cross(b.position - a.position, c.position - a.position);
                    a.normal += fn; b.normal += fn; c.normal += fn;
                }
                for (Vertex& v : md.vertices) v.normal = glm::length(v.normal) > 0 ? glm::normalize(v.normal) : glm::vec3(0, 1, 0);
            }

            ModelAsset::Part part;
            part.name = mesh.name ? mesh.name : "Mesh " + std::to_string(mi);
            part.material = prim.material ? int(prim.material - data->materials) : -1;
            part.skinned = skinned;
            FinishPart(*model, part, md);
            meshParts[mi].push_back(int(model->parts.size()));
            model->parts.push_back(std::move(part));
            model->partData.push_back(std::move(md));
        }
    }

    for (size_t i = 0; i < data->nodes_count; ++i) {
        const cgltf_node& n = data->nodes[i];
        ModelAsset::Node node;
        node.name = n.name ? n.name : (n.mesh ? "Mesh" : "Node");
        if (n.has_matrix) {
            glm::mat4 m;
            std::memcpy(&m[0][0], n.matrix, sizeof(float) * 16);
            glm::vec3 skew;
            glm::vec4 persp;
            glm::decompose(m, node.scale, node.rotation, node.position, skew, persp);
        } else {
            if (n.has_translation) node.position = {n.translation[0], n.translation[1], n.translation[2]};
            if (n.has_rotation) node.rotation = glm::quat(n.rotation[3], n.rotation[0], n.rotation[1], n.rotation[2]);
            if (n.has_scale) node.scale = {n.scale[0], n.scale[1], n.scale[2]};
        }
        node.position = MirrorX(node.position);
        node.rotation = MirrorX(node.rotation);
        if (n.mesh)
            node.parts = meshParts[size_t(n.mesh - data->meshes)];
        node.parent = n.parent ? int(n.parent - data->nodes) : -1;
        model->nodes.push_back(std::move(node));
    }

    for (size_t i = 0; i < data->animations_count; ++i) {
        const cgltf_animation& anim = data->animations[i];
        float duration = 0.0f;
        for (size_t s = 0; s < anim.samplers_count; ++s)
            if (anim.samplers[s].input && anim.samplers[s].input->has_max)
                duration = std::max(duration, anim.samplers[s].input->max[0]);
        model->animations.push_back({anim.name ? anim.name : "Animation " + std::to_string(i), duration});
    }
    cgltf_free(data);
    return model;
}

} // namespace

namespace ModelImporter {

bool IsModelFile(const fs::path& path)
{
    std::string ext = Lower(path.extension().string());
    return ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb";
}

std::unique_ptr<ModelAsset> Load(const fs::path& assetRoot, const std::string& assetPath, std::string& error)
{
    fs::path file = assetRoot / Platform::Utf8ToPath(assetPath);
    TextureResolver textures(assetRoot, file);
    std::string ext = Lower(file.extension().string());
    std::unique_ptr<ModelAsset> model = ext == ".gltf" || ext == ".glb" ? LoadGltf(file, textures, error)
                                                                          : LoadUfbx(file, textures, error);
    if (!model)
        return nullptr;
    model->assetPath = assetPath;
    bool first = true;
    for (const ModelAsset::Part& p : model->parts) {
        if (p.vertexCount == 0)
            continue;
        model->boundsMin = first ? p.boundsMin : glm::min(model->boundsMin, p.boundsMin);
        model->boundsMax = first ? p.boundsMax : glm::max(model->boundsMax, p.boundsMax);
        first = false;
    }
    return model;
}

} // namespace ModelImporter

EntityID InstantiateModel(Scene& scene, const ModelAsset& model, EntityID parent)
{
    Entity& root = scene.CreateEntity(Platform::Utf8ToPath(model.assetPath).stem().string());
    root.parent = parent;
    std::vector<EntityID> ids(model.nodes.size(), 0);

    auto addRenderer = [&](Entity& e, int part) {
        e.meshRenderer = MeshRendererComponent{};
        e.meshRenderer->mesh = model.PartMeshName(part);
        int mat = model.parts[size_t(part)].material;
        if (mat >= 0 && mat < int(model.materials.size())) {
            e.meshRenderer->color = model.materials[size_t(mat)].baseColor;
            e.meshRenderer->texture = model.materials[size_t(mat)].baseColorTexture;
        }
    };

    // Nodes are created in file order; parents may come later, so link afterwards.
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const ModelAsset::Node& node = model.nodes[i];
        Entity& e = scene.CreateEntity(node.name);
        e.transform.position = node.position;
        e.transform.rotation = node.rotation;
        e.transform.scale = node.scale;
        ids[i] = e.id;
        for (size_t p = 0; p < node.parts.size(); ++p) {
            if (p == 0) {
                addRenderer(e, node.parts[p]);
            } else {
                // Extra material slots become child objects.
                Entity& sub = scene.CreateEntity(node.name + " (" + std::to_string(p) + ")");
                sub.parent = e.id;
                addRenderer(sub, node.parts[p]);
            }
        }
    }
    EntityID rootId = root.id;
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        Entity* e = scene.Get(ids[i]);
        int p = model.nodes[i].parent;
        e->parent = p >= 0 && size_t(p) < ids.size() ? ids[size_t(p)] : rootId;
    }
    scene.UpdateWorldTransforms();
    return rootId;
}

} // namespace ze
