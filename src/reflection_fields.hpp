#pragma once

#include "reflection_types.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>

#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

enum FieldFlags : uint32_t {
    Editable     = 1 << 0,
    ReadOnly     = 1 << 1
};

struct Container{
    // element info
    TypeID elementType;
    size_t elementSize;

    TypeID editorElementType;

    // common queries
    size_t (*size)(void* container);
    bool   (*isAssociative)();

    // element conversion
    void (*elementRead)(const void* element, void* outEditorValue);
    void (*elementWrite)(void* component, void* element, const void* editorValue);

    // SEQUENTIAL containers (vectors)
    void* (*getByIndex)(void* container, size_t index);
    void  (*insertAt)(void* container, size_t index, const void* element);
    void  (*eraseAt)(void* container, size_t index);

    // ASSOCIATIVE containers (map, set)
    void* (*findByKey)(void* container, const void* key);
    void  (*insertByKey)(void* container, const void* key, const void* value);
    void  (*eraseByKey)(void* container, const void* key);

    // lifecycle
    void (*clear)(void* container);
};

struct FieldInfo {
    // Identity
    const char* name;         // Field name
    TypeID type;      // Type stored in the component

    // Memory access
    uint32_t offset;          // Offset in the struct (used if no getter/setter)

    // Behavior

    // Optional getter/setter functions. If nullptr, read/write directly using offset.
    void (*read)(void* object, void* out_value);
    void (*write)(void* object, const void* value);

    // Helpers
    void (*copy)(void* src, const void* dst);  // Copies value
    bool (*equals)(const void* a, const void* b); // Compares values

    // Editor metadata

    uint32_t flags;           // Editable / ReadOnly
    float min;                // Optional min value for editor widgets
    float max;                // Optional max value for editor widgets

    const Container* container = nullptr;
    const EnumDescriptor* enumDesc = nullptr;
};

struct ClassDescriptor{
    std::string name;
    std::vector<FieldInfo*> fields;
};

template<typename StorageT, typename EditorT = StorageT>
Container MakeVectorContainer(void (*elementRead)(const void*, void*) = nullptr,void (*elementWrite)(void*, void*, const void*) = nullptr) {
    Container c{};

    c.elementType = GetTypeIDFromString(typeid(StorageT).name());
    c.elementSize = sizeof(StorageT);
    c.editorElementType = GetTypeIDFromString(typeid(EditorT).name());

    if constexpr (std::is_same_v<StorageT, EditorT>) {
        c.elementRead = elementRead ? elementRead :
            [](const void* e, void* out) {
                *static_cast<EditorT*>(out) =
                    *static_cast<const StorageT*>(e);
            };

        c.elementWrite = elementWrite ? elementWrite :
            [](void* e, const void* in) {
                *static_cast<StorageT*>(e) =
                    *static_cast<const EditorT*>(in);
            };
    } else {
        c.elementRead = elementRead;
        c.elementWrite = elementWrite;
    }

    c.size = [](void* c) -> size_t {
        return static_cast<std::vector<StorageT>*>(c)->size();
    };

    c.isAssociative = []() { return false; };

    c.getByIndex = [](void* c, size_t i) -> void* {
        return &(*static_cast<std::vector<StorageT>*>(c))[i];
    };

    c.insertAt = [](void* c, size_t i, const void* v) {
        auto& vec = *static_cast<std::vector<StorageT>*>(c);
        vec.insert(vec.begin() + i, *static_cast<const StorageT*>(v));
    };

    c.eraseAt = [](void* c, size_t i) {
        auto& vec = *static_cast<std::vector<StorageT>*>(c);
        vec.erase(vec.begin() + i);
    };

    c.clear = [](void* c) {
        static_cast<std::vector<StorageT>*>(c)->clear();
    };

    return c;
}

template<typename K, typename V, typename EditorV = V>
Container MakeMapContainer(void (*elementRead)(const void*, void*) = nullptr,void (*elementWrite)(void*, void*, const void*) = nullptr) {
    Container c{};

    c.elementType = GetTypeIDFromString(typeid(V).name());
    c.elementSize = sizeof(V);
    c.editorElementType = GetTypeIDFromString(typeid(EditorV).name());

    if constexpr (std::is_same_v<V, EditorV>) {
        c.elementRead = elementRead ? elementRead :
            [](const void* e, void* out) {
                *static_cast<EditorV*>(out) =
                    *static_cast<const V*>(e);
            };

        c.elementWrite = elementWrite ? elementWrite :
            [](void* e, const void* in) {
                *static_cast<V*>(e) =
                    *static_cast<const EditorV*>(in);
            };
    } else {
        c.elementRead = elementRead;
        c.elementWrite = elementWrite;
    }

    c.size = [](void* c) -> size_t {
        return static_cast<std::map<K, V>*>(c)->size();
    };

    c.isAssociative = []() { return true; };

    // sequential ops unused
    c.getByIndex = nullptr;
    c.insertAt = nullptr;
    c.eraseAt = nullptr;

    // associative ops
    c.findByKey = [](void* c, const void* key) -> void* {
        auto& m = *static_cast<std::map<K, V>*>(c);
        auto it = m.find(*static_cast<const K*>(key));
        return it == m.end() ? nullptr : &it->second;
    };

    c.insertByKey = [](void* c, const void* key, const void* value) {
        auto& m = *static_cast<std::map<K, V>*>(c);
        m[*static_cast<const K*>(key)] =
            *static_cast<const V*>(value);
    };

    c.eraseByKey = [](void* c, const void* key) {
        static_cast<std::map<K, V>*>(c)->erase(
            *static_cast<const K*>(key));
    };

    c.clear = [](void* c) {
        static_cast<std::map<K, V>*>(c)->clear();
    };

    return c;
}

QualType getInnerTypeFromStdVector(QualType type, ASTContext& Context) {
    type = type.getCanonicalType().getUnqualifiedType();

    // Makin sure it's a class template specialization
    if (const auto* recordType = type->getAs<RecordType>()) {
        const auto* decl = recordType->getDecl();
        if (const auto* tmplSpec = dyn_cast<ClassTemplateSpecializationDecl>(decl)) {
            const TemplateArgument& arg = tmplSpec->getTemplateArgs()[0];
            QualType innerType = arg.getAsType();

            // Keep shared_ptr as-is
            return innerType;
        }
    }

    return QualType(); // failed to extract
}

std::pair<QualType, QualType> getKeyValueTypeFromStdMap(QualType type, ASTContext& Context) {
    type = type.getCanonicalType().getUnqualifiedType();

    // Make sure it's a class template specialization
    if (const auto* recordType = type->getAs<RecordType>()) {
        const auto* decl = recordType->getDecl();
        if (const auto* tmplSpec = dyn_cast<ClassTemplateSpecializationDecl>(decl)) {
            const TemplateArgumentList& argsList = tmplSpec->getTemplateArgs();

            if (argsList.size() < 2)
                return {QualType(), QualType()};

            QualType keyType   = argsList[0].getAsType();
            QualType valueType = argsList[1].getAsType();

            // Keep shared_ptr or unique_ptr as-is for valueType
            return {keyType, valueType};
        }
    }

    return {QualType(), QualType()}; // failed to extract
}

struct StructDescriptor{
    std::string name;
    std::vector<FieldInfo*> fields;
};

class FieldHandler : public MatchFinder::MatchCallback {
public:
    void run(const MatchFinder::MatchResult &Result) override {
        if (!Result.SourceManager) return;

        const auto* Decl = Result.Nodes.getNodeAs<CXXRecordDecl>("record");
        if (!Decl) return;

        const auto &SM = *Result.SourceManager;
        const CXXRecordDecl* Definition = Decl->getDefinition();
        if (!Definition) {
            // No definition in this TU, skip it
            return;
        }

        std::string filename = SM.getFilename(Definition->getLocation()).str();

        // Initialize storage for this file if not already
        auto& fileEntries = perFileGenerated[filename];

        // Check all annotations
        for (auto* Attr : Decl->specific_attrs<AnnotateAttr>()) {
            std::string annotation = Attr->getAnnotation().str();

            if (annotation == "class") {
                std::string code = handleClass(Decl, SM);
                if (!code.empty())
                    fileEntries.push_back(code);
            }
            else if (annotation == "struct") {
                std::string code = handleStruct(Decl, SM);
                if (!code.empty())
                    fileEntries.push_back(code);
            }
        }
    }

    // Call this at the end of the tool run to write the file
    void writeAllFiles() {
        for (auto& [file, codeList] : perFileGenerated) {
            std::filesystem::path p(file);
            std::string outFile = (p.parent_path() / (p.stem().string() + ".reflection.hpp")).string();
            std::ofstream out(outFile, std::ios::trunc);
            if (!out.is_open()) {
                llvm::errs() << "Failed to open file: " << outFile << "\n";
                continue;
            }

            out << "#pragma once\n\n";
            out << "#include \"" << p.filename().string() << "\"\n";
            out << "#include \"engine/core/reflection_fields.hpp\"\n\n";

            for (auto& code : codeList)
                out << code << "\n";
                
            out.close();
        }

        llvm::outs() << "Successfully wrote all reflection files" << "\n";
    }

private:
    std::map<std::string, std::vector<std::string>> perFileGenerated;
    
    std::string handleClass(const CXXRecordDecl* C,const SourceManager& SM) {
        if (!C || !C->isThisDeclarationADefinition())
            return {};

        std::ostringstream out;

        out << "// Reflection for class " << C->getNameAsString() << "\n\n";

        std::vector<std::string> fieldInfos;

        for (const FieldDecl* F : C->fields()) {
            for (auto *attr : F->attrs()) {
                if (const auto *aa = dyn_cast<AnnotateAttr>(attr)) {
                    std::string annotation = aa->getAnnotation().str();
                    if (annotation.find("field") == 0) {
                        std::string fieldCode = handleField(F, SM);
                        if (!fieldCode.empty()) {
                            fieldInfos.push_back(C->getNameAsString() + "_" + F->getNameAsString() + "_info");
                            out << fieldCode;
                        }
                    }
                }
            }
        }

        out << "inline ClassDescriptor "
            << C->getQualifiedNameAsString() << "::descriptor = {\n"
            << "    \"" << C->getNameAsString() << "\",\n    {\n";

        for (auto& f : fieldInfos)
            out << "        &" << f << ",\n";

        out << "    }\n};\n";

        return out.str();
    }

    std::string  handleStruct(const CXXRecordDecl* S, const clang::SourceManager &SM){
        if (!S || !S->isThisDeclarationADefinition())
            return {};

        std::ostringstream out;

        out << "// Reflection for struct " << S->getNameAsString() << "\n\n";

        std::vector<std::string> fieldInfos;

        for (const FieldDecl* F : S->fields()) {
            for (auto *attr : F->attrs()) {
                if (const auto *aa = dyn_cast<AnnotateAttr>(attr)) {
                    std::string annotation = aa->getAnnotation().str();
                    if (annotation.find("field") == 0) {
                        std::string fieldCode = handleField(F, SM);
                        if (!fieldCode.empty()) {
                            fieldInfos.push_back(S->getNameAsString() + "_" + F->getNameAsString() + "_info");
                            out << fieldCode;
                        }
                    }
                }
            }
        }

        std::string structName = S->getNameAsString();
        std::string structQualifiedName = S->getQualifiedNameAsString();

        out << "inline StructDescriptor "
            << structName << "_descriptor = {\n"
            << "    \"" << structName << "\",\n"
            << "    {\n";

        for (auto& f : fieldInfos)
            out << "        &" << f << ",\n";

        out << "    },\n"
            << "    sizeof(" << structQualifiedName << "),\n"
            << "    [](void* p) { new (p) " << structQualifiedName << "(); },\n"
            << "    [](void* p) { static_cast<" << structQualifiedName << "*>(p)->~"
            << structName << "(); },\n"
            << "    [](void* d, const void* s) {\n"
            << "        *static_cast<" << structQualifiedName << "*>(d) =\n"
            << "        *static_cast<const " << structQualifiedName << "*>(s);\n"
            << "    },\n"
            << "    [](const void* a, const void* b) {\n"
            << "        return *static_cast<const " << structQualifiedName << "*>(a)\n"
            << "            == *static_cast<const " <<structQualifiedName<<"*>(b);\n"
            << "    }\n"
            << "};\n";

        return out.str();
    }

    std::string handleField(const FieldDecl* F, const clang::SourceManager &SM){
        
        if (!F || !SM.isWrittenInMainFile(F->getLocation()))
            return {};

        std::ostringstream out;

        const auto* Parent = dyn_cast<CXXRecordDecl>(F->getParent());
        if (!Parent)
            return {};

        // ---------- parse annotations ----------
        bool isEditable = false;
        bool isReadOnly = false;

        std::string readFunc, writeFunc, copyFunc, equalsFunc;
        std::string customType;
        std::string range;
        std::string creadFunc, cwriteFunc;

        for (const Attr* attr : F->attrs()) {
            if (const auto* aa = dyn_cast<AnnotateAttr>(attr)) {
                if (aa->getAnnotation() == "field")
                    continue; // skip kind tag

                std::istringstream ss(aa->getAnnotation().str());
                std::string token;
                while (std::getline(ss, token, ',')) {
                    token.erase(0, token.find_first_not_of(" \t"));
                    token.erase(token.find_last_not_of(" \t") + 1);

                    if (token == "Editable") isEditable = true;
                    else if (token == "ReadOnly") isReadOnly = true;
                    else if (token.find("read=") == 0)   readFunc   = token.substr(5);
                    else if (token.find("write=") == 0)  writeFunc  = token.substr(6);
                    else if (token.find("copy=") == 0)   copyFunc   = token.substr(5);
                    else if (token.find("equals=") == 0) equalsFunc = token.substr(7);
                    else if (token.find("type=") == 0)   customType = token.substr(5);
                    else if (token.find("range=") == 0)  range      = token.substr(6);
                    else if (token.find("cread=") == 0)  creadFunc  = token.substr(6);
                    else if (token.find("cwrite=") == 0) cwriteFunc = token.substr(7);
                }
            }
        }

        if (isEditable == isReadOnly)
            return {}; // invalid or irrelevant

        // ---------- type inspection ----------
        ASTContext& Ctx = F->getASTContext();
        PrintingPolicy policy(Ctx.getLangOpts());
        policy.SuppressTagKeyword = true;

        QualType fieldType = F->getType();
        std::string typeName = fieldType.getAsString(policy);

        bool isVector = typeName.find("std::vector") != std::string::npos;
        bool isMap    = typeName.find("std::map")    != std::string::npos;
        bool isEnum   = fieldType->isEnumeralType();
        bool isStruct = false;
        if (const RecordType* RT = fieldType->getAs<RecordType>()) {
            const CXXRecordDecl* RD = dyn_cast<CXXRecordDecl>(RT->getDecl());
            if (RD && RD->getNameAsString() == "InstancedStruct") {
                isStruct = true;
            }
        }
        const std::string structDescVar = fieldType.getAsString() + "_descriptor";

        std::string fieldName = F->getNameAsString();
        std::string containerVar = Parent->getNameAsString() + "_" + fieldName + "_container";
        std::string enumDescVar;

        // ---------- container / enum generation ----------
        if (isVector) {
            std::string inner =
                getInnerTypeFromStdVector(fieldType, Ctx).getAsString(policy);

            out << "static Container " << containerVar
                << " = MakeVectorContainer<"
                << inner << ", " << (customType.empty() ? inner : customType)
                << ">("
                << (creadFunc.empty()  ? "nullptr" : creadFunc) << ", "
                << (cwriteFunc.empty() ? "nullptr" : cwriteFunc)
                << ");\n\n";
            
            typeName = "std::vector<";
            customType = "std::vector<";
        }
        else if (isMap) {
            auto [K, V] = getKeyValueTypeFromStdMap(fieldType, Ctx);

            out << "static Container " << containerVar
                << " = MakeMapContainer<"
                << K.getAsString(policy) << ", "
                << V.getAsString(policy) << ", "
                << (customType.empty() ? V.getAsString(policy) : customType)
                << ">("
                << (creadFunc.empty()  ? "nullptr" : creadFunc) << ", "
                << (cwriteFunc.empty() ? "nullptr" : cwriteFunc)
                << ");\n\n";
            
            typeName = "std::map<";
            customType = "std::map<";
        }
        else if (isEnum) {

            const EnumDecl* E = fieldType->getAs<EnumType>()->getDecl();
            enumDescVar = E->getNameAsString() + "_descriptor";

            out << "static EnumDescriptor " << enumDescVar << " = {\n";
            out << "    \"" << E->getNameAsString() << "\",\n    {\n";
            for (const auto* it : E->enumerators()) {
                out << "        { " << it->getInitVal().getSExtValue()
                    << ", ""\"" << it->getNameAsString() << "\" },\n";
            }
            out << "    },\n";
            out << "    sizeof(" << fieldType.getAsString(policy) << ")\n";
            out << "};\n\n";

            typeName = "enum";
        }
        else if(isStruct){
            
            typeName = "struct";
            customType = "struct";
        }

        // ---------- FieldInfo ----------

        out << "inline FieldInfo "
            << Parent->getNameAsString() << "_" << fieldName << "_info = {\n"
            << "    \"" << fieldName << "\",\n"
            << "    TypeID::"
            << GetStringFromTypeID(GetTypeIDFromString(
                customType.empty() ? typeName : customType))
            << ",\n"
            << "    offsetof(" 
            << Parent->getQualifiedNameAsString() 
            << ", " 
            << fieldName 
            << "),\n"
            << "    " << (readFunc.empty()   ? "nullptr" : readFunc) << ",\n"
            << "    " << (writeFunc.empty()  ? "nullptr" : writeFunc) << ",\n"
            << "    " << (copyFunc.empty()   ? "nullptr" : copyFunc) << ",\n"
            << "    " << (equalsFunc.empty() ? "nullptr" : equalsFunc) << ",\n"
            << "    " << (isEditable ? "Editable" : "ReadOnly") << ",\n"
            << "    " << (range.empty() ? "0, 0" : range) << ",\n"
            << "    " << ((isVector || isMap) ? "&" + containerVar : "nullptr") << ",\n"
            << "    " << (isEnum ? "&" + enumDescVar : "nullptr") << "\n"
            << "};\n\n";

        return out.str();
    }
};