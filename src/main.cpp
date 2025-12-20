#include "reflection_fields.hpp"

#include <filesystem>
#include <vector>
#include <string>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
namespace fs = std::filesystem;

static llvm::cl::OptionCategory ToolCategory("pulse-reflect options");
static llvm::cl::opt<std::string> FileOpt("f", llvm::cl::desc("Single file"));
static llvm::cl::opt<std::string> DirOpt("dir", llvm::cl::desc("Directory to scan"));
static llvm::cl::opt<bool> RecursiveOpt("recursive", llvm::cl::desc("Recurse into directories"));
static llvm::cl::opt<std::string> GCCPathOpt("gcc",llvm::cl::desc("Path to GCC toolchain root"),llvm::cl::Required);
static llvm::cl::opt<std::string> CPPPathOpt("cpp",llvm::cl::desc("Path to C++ standard library headers"),llvm::cl::Required);
static llvm::cl::list<std::string> IncludeDirs(
    "I",
    llvm::cl::desc("Project include directories (can specify multiple, mandatory)"),
    llvm::cl::Required,
    llvm::cl::ZeroOrMore
);


void collectFiles(const std::filesystem::path &path, bool recursive, std::vector<std::string> &outFiles) {
    if (!std::filesystem::exists(path)) return;

    if (std::filesystem::is_regular_file(path)) {
        outFiles.push_back(path.string());
        return;
    }

    if (recursive) {
        for (auto &entry : std::filesystem::recursive_directory_iterator(path)) {
            if (entry.is_regular_file()) {
                outFiles.push_back(entry.path().string());
            }
        }
    } else {
        for (auto &entry : std::filesystem::directory_iterator(path)) {
            if (entry.is_regular_file()) {
                outFiles.push_back(entry.path().string());
            }
        }
    }
}


class ReflectionFrontendAction : public ASTFrontendAction {
    FieldHandler Handler;
    MatchFinder Finder;
public:
    ReflectionFrontendAction() {
        Finder.addMatcher(
            cxxRecordDecl(isDerivedFrom("Pulse::Engine::ECS::Components::Component")).bind("componentClass"),
            &Handler
        );
    }

    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef InFile) override {
        return Finder.newASTConsumer();
    }
};


int main(int argc, const char **argv) {
    llvm::cl::ParseCommandLineOptions(argc, argv, "PulseReflect tool\n");


    std::vector<std::string> files;

    if (!FileOpt.empty()) files.push_back(FileOpt);
    if (!DirOpt.empty()) collectFiles(DirOpt.getValue(), RecursiveOpt, files);

    if (files.empty()) {
        llvm::errs() << "No files specified!\n";
        return 1;
    }

    std::vector<std::string> defaultFlags = {
        "-std=c++17",
        "--target=x86_64-pc-linux-gnu",
        "--gcc-toolchain=" + GCCPathOpt,
        "-isystem", GCCPathOpt + "/include",
        "-isystem", GCCPathOpt + "/include-fixed",
        "-isystem", CPPPathOpt,
        "-isystem", CPPPathOpt + "/x86_64-pc-linux-gnu",
    };

    for (const auto &dir : IncludeDirs) {
        defaultFlags.push_back("-I" + dir);
    }

    FixedCompilationDatabase Compilations(".", defaultFlags);
    ClangTool Tool(Compilations, files);

    FieldHandler Handler;
    return Tool.run(newFrontendActionFactory<ReflectionFrontendAction>().get());
}