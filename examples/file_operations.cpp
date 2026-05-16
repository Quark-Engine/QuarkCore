#include "QuarkCore/QuarkCore.hpp"

int main() {
    qc::InitWindow(1280, 720, "QuarkCore File Operations Example", qc::RendererType::OpenGL);
    qc::SetLogLevel(qc::LogLevel::Info);
    qc::TraceLog(qc::LogLevel::Info, "FILE_EXAMPLE", "File operations example started");

    const char* appDir = qc::GetApplicationDirectory();
    const char* workDir = qc::GetWorkingDirectory();
    
    qc::TraceLog(qc::LogLevel::Info, "FILE_EXAMPLE", qc::TextFormat("App directory: %s", appDir));
    qc::TraceLog(qc::LogLevel::Info, "FILE_EXAMPLE", qc::TextFormat("Working directory: %s", workDir));

    const char* testDir = "test_data";
    if (qc::MakeDirectory(testDir) == 0) {
        qc::TraceLog(qc::LogLevel::Info, "FILE_EXAMPLE", qc::TextFormat("Created directory: %s", testDir));
    } else {
        qc::TraceLog(qc::LogLevel::Warn, "FILE_EXAMPLE", qc::TextFormat("Failed to create directory: %s", testDir));
    }

    const char* subDir = "test_data/subdirectory";
    if (qc::MakeDirectory(subDir) == 0) {
        qc::TraceLog(qc::LogLevel::Info, "FILE_EXAMPLE", qc::TextFormat("Created subdirectory: %s", subDir));
    }

    qc::FilePathList files = qc::LoadDirectoryFiles(".");
    qc::TraceLog(qc::LogLevel::Info, "FILE_EXAMPLE", 
        qc::TextFormat("Found %d files in current directory", files.count));
    for (unsigned int i = 0; i < files.count && i < 5; ++i) {
        qc::TraceLog(qc::LogLevel::Info, "FILE_EXAMPLE", 
            qc::TextFormat("  - %s", files.paths[i]));
    }

    const char* testNames[] = { "valid_file.txt", "<invalid>.txt", "con", "file|name.txt" };
    for (int i = 0; i < 4; ++i) {
        bool valid = qc::IsFileNameValid(testNames[i]);
        qc::TraceLog(qc::LogLevel::Info, "FILE_EXAMPLE",
            qc::TextFormat("Filename '%s' is %s", testNames[i], valid ? "valid" : "invalid"));
    }

    bool isSandboxFile = qc::IsPathFile("examples/sandbox.cpp");
    qc::TraceLog(qc::LogLevel::Info, "FILE_EXAMPLE",
        qc::TextFormat("examples/sandbox.cpp is %s", isSandboxFile ? "a file" : "not a file"));

    unsigned int fileCount = qc::GetDirectoryFileCount(".");
    qc::TraceLog(qc::LogLevel::Info, "FILE_EXAMPLE",
        qc::TextFormat("Total files in current directory: %d", fileCount));

    const char* parentDir = qc::GetPrevDirectoryPath(workDir);
    qc::TraceLog(qc::LogLevel::Info, "FILE_EXAMPLE",
        qc::TextFormat("Parent directory: %s", parentDir));

    qc::Font defaultFont = qc::GetDefaultFont();
    std::string displayText = "";
    
    while (!qc::WindowShouldClose()) {
        qc::BeginDrawing();
        qc::ClearBackground(qc::Color{20, 24, 32, 255});

        int y = 20;
        qc::DrawText("QuarkCore File Operations Example", 20, y, 32, qc::YELLOW);
        y += 50;

        qc::DrawText(qc::TextFormat("App Directory: %s", appDir), 20, y, 20, qc::WHITE);
        y += 30;

        qc::DrawText(qc::TextFormat("Working Directory: %s", workDir), 20, y, 20, qc::WHITE);
        y += 30;

        qc::DrawText(qc::TextFormat("Files in directory: %d", files.count), 20, y, 20, qc::WHITE);
        y += 30;

        qc::DrawText("Valid Filenames:", 20, y, 20, qc::GREEN);
        y += 25;
        qc::DrawText("  - valid_file.txt", 20, y, 18, qc::LIGHTGRAY);
        y += 25;

        qc::DrawText("Invalid Filenames:", 20, y, 20, qc::RED);
        y += 25;
        qc::DrawText("  - <invalid>.txt (contains <>)", 20, y, 18, qc::LIGHTGRAY);
        y += 25;
        qc::DrawText("  - con (reserved name)", 20, y, 18, qc::LIGHTGRAY);
        y += 25;
        qc::DrawText("  - file|name.txt (contains |)", 20, y, 18, qc::LIGHTGRAY);
        y += 30;

        qc::DrawText(qc::TextFormat("sandbox.cpp is a file: %s", 
            isSandboxFile ? "Yes" : "No"), 20, y, 20, qc::WHITE);
        y += 30;

        qc::DrawText(qc::TextFormat("Created test directory: %s", testDir), 20, y, 20, qc::SKYBLUE);
        y += 30;

        qc::DrawText("Press SPACE to reload directory list", 20, y, 18, qc::GRAY);

        if (qc::IsKeyPressed(qc::KeyboardKey::Space)) {
            qc::UnloadDirectoryFiles(files);
            files = qc::LoadDirectoryFiles(".");
            qc::TraceLog(qc::LogLevel::Info, "FILE_EXAMPLE", 
                qc::TextFormat("Reloaded: %d files found", files.count));
        }

        qc::EndDrawing();
    }

    qc::UnloadDirectoryFiles(files);
    qc::CloseWindow();
    
    qc::TraceLog(qc::LogLevel::Info, "FILE_EXAMPLE", "Example finished");
    return 0;
}
