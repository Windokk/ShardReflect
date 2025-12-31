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
    void (*elementWrite)(void* element, const void* editorValue);

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

template<typename StorageT, typename EditorT = StorageT>
Container MakeVectorContainer(void (*elementRead)(const void*, void*) = nullptr,void (*elementWrite)(void*, const void*) = nullptr) {
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
Container MakeMapContainer(void (*elementRead)(const void*, void*) = nullptr,void (*elementWrite)(void*, const void*) = nullptr) {
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

    // Make sure it's a class template specialization
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

// Field reflection haandler
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
            std::string creadFuncName = "";
            std::string cwriteFuncName = "";

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
                        else if (token.find("cread=") == 0) creadFuncName = token.substr(6);
                        else if (token.find("cwrite=") == 0) cwriteFuncName = token.substr(7);
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

            PrintingPolicy policy(Result.Context->getLangOpts());
            policy.SuppressTagKeyword = true; // removes "class"/"struct"/"enum"

            if (isVector) {
                std::string inner = getInnerTypeFromStdVector(field->getType(), *Result.Context).getAsString(policy);
                
                out << "static Container " << containerVarName
                        << " = MakeVectorContainer<"
                        << inner <<","<< customTypeName << ">(";
                // elementRead
                if (!creadFuncName.empty())
                    out << creadFuncName<<",";
                else
                    out << "nullptr,";

                // elementWrite
                if (!cwriteFuncName.empty())
                    out << cwriteFuncName;
                else
                    out << "nullptr";

                out << ");\n";

            }
            else if (isMap) {
                std::pair<QualType, QualType> keyValueType = getKeyValueTypeFromStdMap(field->getType(), *Result.Context);

                std::string keyType = keyValueType.first.getAsString(policy);
                std::string valueType = keyValueType.second.getAsString(policy);

                out << "static Container " << containerVarName
                    << " = MakeMapContainer<"
                    << keyType << ", " << valueType <<","<< customTypeName << ">(";

                // elementRead
                if (!creadFuncName.empty())
                    out << creadFuncName;
                else
                    out << "nullptr";

                out << ", ";

                // elementWrite
                if (!cwriteFuncName.empty())
                    out << cwriteFuncName;
                else
                    out << "nullptr";

                out << ");\n";
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
            
            out << "inline FieldInfo " << ClassDecl->getNameAsString() << "_" <<fieldName << "_info = {\n";
            out << "    \"" << fieldName << "\",\n";
            out << "    TypeID::" << GetStringFromTypeID(GetTypeIDFromString(customTypeName.empty() ? typeName : ((isVector || isMap || isEnum) ? typeName : customTypeName))) << ",\n";
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

        out << "inline ComponentDescriptor " << ClassDecl->getQualifiedNameAsString() << "::descriptor = {\n";
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