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
static llvm::cl::list<std::string> DirList("dir", llvm::cl::desc("Directory to scan (can specify multiple)"), llvm::cl::ZeroOrMore);
static llvm::cl::opt<bool> RecursiveOpt("recursive", llvm::cl::desc("Recurse into directories"));
static llvm::cl::opt<std::string> ClangPathOpt("clang",llvm::cl::desc("Path to Clang lib root"),llvm::cl::Required);
static llvm::cl::opt<std::string> CPPPathOpt("cpp",llvm::cl::desc("Path to C++ standard library headers"),llvm::cl::Required);
static llvm::cl::list<std::string> IncludeDirs(
    "I",
    llvm::cl::desc("Engine include directories (can specify multiple, mandatory)"),
    llvm::cl::Required,
    llvm::cl::ZeroOrMore
);

std::vector<std::string> ParseIncludeDirs(const std::string &Dirs) {
    std::vector<std::string> result;
    std::stringstream ss(Dirs);
    std::string dir;

    // Split by semicolon
    while (std::getline(ss, dir, ';')) {
        // Trim spaces (if any) around directory names
        dir.erase(dir.find_last_not_of(" \t") + 1);  // trim trailing spaces
        dir.erase(0, dir.find_first_not_of(" \t"));   // trim leading spaces
        
        // Avoid empty entries
        if (!dir.empty()) {
            result.push_back(dir);
        }
    }

    return result;
}

void CollectFiles(const std::filesystem::path &path, bool recursive, std::vector<std::string> &outFiles) {
    if (!std::filesystem::exists(path)) return;

    if (std::filesystem::is_regular_file(path)) {
        outFiles.push_back(path.string());
        return;
    }

    if (recursive) {
        for (auto &entry : std::filesystem::recursive_directory_iterator(path)) {
            if (entry.is_regular_file()) {
                if(entry.path().extension().string() != ".hpp" || entry.path().string().find(".reflection.hpp") != std::string::npos) continue;
                outFiles.push_back(entry.path().string());
            }
        }
    } else {
        for (auto &entry : std::filesystem::directory_iterator(path)) {
            if (entry.is_regular_file()) {
                if(entry.path().extension().string() != ".hpp" || entry.path().string().find(".reflection.hpp") != std::string::npos) continue;
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

void Remove_duplicates(std::vector<std::string>& vec) {
    
    std::sort(vec.begin(), vec.end());

    auto last = std::unique(vec.begin(), vec.end());

    vec.erase(last, vec.end());
}

int main(int argc, const char **argv) {
    llvm::cl::ParseCommandLineOptions(argc, argv, "PulseReflect tool\n");


    std::vector<std::string> files;

    if (!FileOpt.empty()) files.push_back(FileOpt);
    for(const auto &dir : DirList){
        CollectFiles(dir, RecursiveOpt, files);
    }

    if (files.empty()) {
        llvm::errs() << "No files specified!\n";
        return 1;
    }

    std::vector<std::string> defaultFlags;

#ifdef _WIN32
    // ---- Windows (MinGW) ----
    defaultFlags = {
        "-std=c++17",
        "--target=x86_64-w64-windows-gnu",

        // MinGW C++ standard library and system headers
        "-isystem", CPPPathOpt + "/include",
        "-isystem", CPPPathOpt + "/x86_64-w64-mingw32/include",

        // Clang internal headers
        "-resource-dir=" + ClangPathOpt
    };

#else
    // ---- Linux ----
    defaultFlags = {
        "-std=c++17",
        "--target=x86_64-pc-linux-gnu",

        "-isystem", CPPPathOpt,
        "-isystem", CPPPathOpt + "/x86_64-pc-linux-gnu",

        "-resource-dir=" + ClangPathOpt
    };
#endif

    for (const auto &include : IncludeDirs) {
        for (const auto& dir : ParseIncludeDirs(include)) {
            defaultFlags.push_back("-I");  // Add -I as a separate argument
            defaultFlags.push_back(dir);   // Add the directory as a separate argument
        }
    }

    Remove_duplicates(files);

    for(std::string file : files){
        llvm::outs() << "Generating reflection for file: " << file << "\n";
    }

    FixedCompilationDatabase Compilations(".", defaultFlags);
    ClangTool Tool(Compilations, files);

    FieldHandler Handler;
    return Tool.run(newFrontendActionFactory<ReflectionFrontendAction>().get());
}