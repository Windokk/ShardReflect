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

    // common queries
    size_t (*size)(void* container);
    bool   (*isAssociative)();

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

struct ComponentDescriptor{
    std::string name;
    std::vector<FieldInfo*> fields;
};

template<typename T>
Container MakeVectorContainer() {
    static Container container {
        // elementType 
        GetTypeIDFromString(typeid(T).name()),

        // elementSize
        sizeof(T),

        // size
        [](void* container) -> size_t {
            return static_cast<std::vector<T>*>(container)->size();
        },

        // isAssociative
        []() { return false; },

        // getByIndex
        [](void* container, size_t i) -> void* {
            return &(*static_cast<std::vector<T>*>(container))[i];
        },

        // insertAt
        [](void* container, size_t i, const void* v) {
            auto& cont = *static_cast<std::vector<T>*>(container);
            cont.insert(cont.begin() + i, *static_cast<const T*>(v));
        },

        // eraseAt
        [](void* container, size_t i) {
            auto& cont = *static_cast<std::vector<T>*>(container);
            cont.erase(cont.begin() + i);
        },

        // findByKey
        nullptr,
        // insertByKey
        nullptr,
        // eraseByKey
        nullptr,

        // clear
        [](void* container) {
            static_cast<std::vector<T>*>(container)->clear();
        }
    };

    return container;
}

template<typename K, typename V>
Container MakeMapContainer() {
    static Container container {
        // elementType 
        GetTypeIDFromString(typeid(V).name()),

        // elementSize
        sizeof(V),

        // size
        [](void* c) -> size_t {
            return static_cast<std::map<K, V>*>(c)->size();
        },

        // isAssociative
        []() { return true; },

        // getByIndex
        nullptr,

        // insertAt
        nullptr,

        // eraseAt
        nullptr,

        // findByKey
        [](void* container, const void* key) -> void* {
            auto& m = *static_cast<std::map<K,V>*>(container);
            auto it = m.find(*static_cast<const K*>(key));
            return it == m.end() ? nullptr : &it->second;
        },

        /* insertByKey */
        [](void* container, const void* key, const void* value) {
            auto& m = *static_cast<std::map<K,V>*>(container);
            m[*static_cast<const K*>(key)] =
                *static_cast<const V*>(value);
        },

        /* eraseByKey */
        [](void* container, const void* key) {
            static_cast<std::map<K,V>*>(container)->erase(
                *static_cast<const K*>(key));
        },

        /* clear */
        [](void* container) {
            static_cast<std::map<K,V>*>(container)->clear();
        }
    };

    return container;
}

// Field reflection handler
class FieldHandler : public MatchFinder::MatchCallback {
public:
    void run(const MatchFinder::MatchResult &Result) override {
        const auto *ClassDecl = Result.Nodes.getNodeAs<CXXRecordDecl>("componentClass");
        if (!ClassDecl || !ClassDecl->isThisDeclarationADefinition())
            return;

        const auto &SM = *Result.SourceManager;
        if (!SM.isWrittenInMainFile(ClassDecl->getLocation()))
            return;

        std::string headerFile = ClassDecl->getASTContext().getSourceManager().getFilename(ClassDecl->getLocation()).str();
        if (headerFile.empty())
            return;

        std::filesystem::path headerPath(headerFile);
        std::string headerFilenameWithExt = headerPath.filename().string();
        std::string headerFilename = headerPath.filename().stem().string();

        std::string outputFile = headerPath.parent_path().string() + "/" + headerFilename + ".reflection.hpp";
        std::ofstream out(outputFile, std::ios::trunc);
        if (!out.is_open()) {
            llvm::errs() << "Failed to open file: " << outputFile << "\n";
            return;
        }
        out << "#include \""+headerFilenameWithExt+"\"\n";
        out << "#include \"engine/core/reflection_fields.hpp\"\n\n";

        out << "//Reflection for component : " << ClassDecl->getNameAsString() << "\n\n";

        for (const auto *field : ClassDecl->fields()) {

            bool isEditable = false;
            bool isReadOnly = false;

            std::string readFuncName = "";
            std::string writeFuncName = "";
            std::string copyFuncName = "";
            std::string equalsFuncName = "";
            std::string customTypeName = "";
            std::string range = "";

            for (auto *attr : field->attrs()) {
                if (const auto *aa = dyn_cast<AnnotateAttr>(attr)) {
                    std::string annotation = aa->getAnnotation().str();
                    std::istringstream ss(annotation);
                    std::string token;
                    while (std::getline(ss, token, ',')) {
                        token.erase(0, token.find_first_not_of(" \t")); // trim
                        token.erase(token.find_last_not_of(" \t")+1);

                        if (token == "Editable") isEditable = true;
                        else if (token == "ReadOnly") isReadOnly = true;
                        else if (token.find("read=") == 0) readFuncName = token.substr(5);
                        else if (token.find("write=") == 0) writeFuncName = token.substr(6);
                        else if (token.find("copy=") == 0) copyFuncName = token.substr(5);
                        else if (token.find("equals=") == 0) equalsFuncName = token.substr(7);
                        else if (token.find("type=") == 0) customTypeName = token.substr(5);
                        else if (token.find("range=") == 0) range = token.substr(6);
                    }
                }
            }

            if ((!isEditable && !isReadOnly) || (isEditable && isReadOnly)) continue;

            uint64_t offsetBits = Result.Context->getFieldOffset(field);
            uint32_t offsetBytes = static_cast<uint32_t>(offsetBits / 8);
            std::string flags = "";
            if(isEditable){
                flags = "Editable";
            }
            else if(isReadOnly){
                flags = "ReadOnly";
            }

            std::string typeName = field->getType().getAsString();
            bool isVector = typeName.find("std::vector") != std::string::npos;
            bool isMap    = typeName.find("std::map") != std::string::npos;
            bool isSet    = typeName.find("std::set") != std::string::npos;
            bool isEnum = field->getType()->isEnumeralType();

            std::string fieldName = field->getNameAsString();
            std::string containerVarName = fieldName + "_container";
            std::string enumDescVar = "";

            if (isVector) {
                out << "static Container " << containerVarName << " = MakeVectorContainer<"
                    << typeName.c_str() << ">();\n";
            }
            else if (isMap) {
                // extract key and value type from string "std::map<K,V>"
                std::string inner = typeName.substr(typeName.find('<') + 1);
                inner = inner.substr(0, inner.find('>')); // "K,V"
                size_t comma = inner.find(',');
                std::string keyType = inner.substr(0, comma);
                std::string valueType = inner.substr(comma+1);

                out << "static Container " << containerVarName << " = MakeMapContainer<"
                    << keyType << "," << valueType << ">();\n";
            }
            else if(isEnum) {
                const EnumDecl* enumDecl = field->getType()->getAs<EnumType>()->getDecl();
                std::string enumName = enumDecl->getNameAsString();

                enumDescVar = enumName + "_descriptor";
                out << "static EnumDescriptor " << enumDescVar << " = {\n";
                out << "    \"" << enumName << "\",\n";
                out << "    {\n";

                for (auto* enumerator : enumDecl->enumerators()) {
                    out << "        { \"" << enumerator->getNameAsString() << "\", "
                        << enumerator->getInitVal().getSExtValue() << " },\n";
                }

                out << "    }\n";
                out << "};\n";
            }
            
            out << "FieldInfo " << ClassDecl->getNameAsString() << "_" <<fieldName << "_info = {\n";
            out << "    \"" << fieldName << "\",\n";
            out << "    TypeID::" << GetStringFromTypeID(GetTypeIDFromString(customTypeName.empty() ? typeName : customTypeName)) << ",\n";
            out << "    " << offsetBytes << ",\n";
            out << "    " << (readFuncName.empty() ? "nullptr" : readFuncName) << ",\n";
            out << "    " << (writeFuncName.empty() ? "nullptr" : writeFuncName) << ",\n";
            out << "    " << (copyFuncName.empty() ? "nullptr" : copyFuncName) << ",\n";
            out << "    " << (equalsFuncName.empty() ? "nullptr" : equalsFuncName) << ",\n";
            out << "    " << flags << ",\n";
            out << "    " << (range == "" ? "0,0": range) << ",\n";
            out << "    " << ((isVector || isMap) ? ("&" + containerVarName) : "nullptr") << ",\n";
            out << "    " << (isEnum ? ("&" + enumDescVar) : "nullptr") << "\n";
            out << "};\n\n";
        }

        out << "ComponentDescriptor " << ClassDecl->getQualifiedNameAsString() << "::descriptor = {\n";
        out << "    \"" << ClassDecl->getNameAsString() << "\",\n";
        out << "    {\n";

        for (const auto *field : ClassDecl->fields()) {
            bool isEditable = false;
            bool isReadOnly = false;

            for (auto *Attr : field->attrs()) {
                if (const auto *aa = dyn_cast<AnnotateAttr>(Attr)) {
                    std::string annotation = aa->getAnnotation().str();
                    std::istringstream ss(annotation);
                    std::string token;
                    while (std::getline(ss, token, ',')) {
                        token.erase(0, token.find_first_not_of(" \t"));
                        token.erase(token.find_last_not_of(" \t") + 1);

                        if (token == "Editable") isEditable = true;
                        else if (token == "ReadOnly") isReadOnly = true;
                    }
                }
            }

            if ((!isEditable && !isReadOnly) || (isEditable && isReadOnly)) continue;

            out << "        &" << ClassDecl->getNameAsString() << "_" <<field->getNameAsString() << "_info,\n";
        }

        out << "    }\n";
        out << "};\n";


        out.close();
        llvm::outs() << "Generated reflection: " << outputFile << "\n";
    }
};