#pragma once

#include "reflection_types.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>

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
};

struct ComponentDescriptor{
    std::string name;
    std::vector<FieldInfo*> fields;
};

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
        out << "#include \"engine/core/reflection_types.hpp\"\n\n";

        out << "//Reflection for component : " << ClassDecl->getNameAsString() << "\n\n";

        for (const auto *Field : ClassDecl->fields()) {

            bool isEditable = false;
            bool isReadOnly = false;

            std::string readFuncName = "";
            std::string writeFuncName = "";
            std::string copyFuncName = "";
            std::string equalsFuncName = "";
            std::string customTypeName = "";
            std::string range = "";

            for (auto *Attr : Field->attrs()) {
                if (const auto *AA = dyn_cast<AnnotateAttr>(Attr)) {
                    std::string annotation = AA->getAnnotation().str();
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

            uint64_t offsetBits = Result.Context->getFieldOffset(Field);
            uint32_t offsetBytes = static_cast<uint32_t>(offsetBits / 8);
            std::string flags = "";
            if(isEditable){
                flags = "Editable";
            }
            else if(isReadOnly){
                flags = "ReadOnly";
            }

            std::string fieldName = Field->getNameAsString();
            std::string typeName = Field->getType().getAsString();

            out << "FieldInfo " << fieldName << "_info = {\n";
            out << "    \"" << fieldName << "\",\n";
            out << "    TypeID::" << GetStringFromTypeID(GetTypeIDFromString(customTypeName.empty() ? typeName : customTypeName)) << ",\n";
            out << "    " << offsetBytes << ",\n";
            out << "    " << (readFuncName.empty() ? "nullptr" : readFuncName) << ",\n";
            out << "    " << (writeFuncName.empty() ? "nullptr" : writeFuncName) << ",\n";
            out << "    " << (copyFuncName.empty() ? "nullptr" : copyFuncName) << ",\n";
            out << "    " << (equalsFuncName.empty() ? "nullptr" : equalsFuncName) << ",\n";
            out << "    " << flags << ",\n";
            out << "    " << range << "\n";
            out << "};\n\n";
        }

        out << "ComponentDescriptor " << ClassDecl->getNameAsString() << "_descriptor = {\n";
        out << "    \"" << ClassDecl->getNameAsString() << "\",\n";
        out << "    {\n";

        for (const auto *Field : ClassDecl->fields()) {
            bool isEditable = false;
            bool isReadOnly = false;

            for (auto *Attr : Field->attrs()) {
                if (const auto *AA = dyn_cast<AnnotateAttr>(Attr)) {
                    std::string annotation = AA->getAnnotation().str();
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

            if (!isEditable && !isReadOnly) continue;

            out << "        &" << Field->getNameAsString() << "_info,\n";
        }

        out << "    }\n";
        out << "};\n";


        out.close();
        llvm::outs() << "Generated reflection: " << outputFile << "\n";
    }
};