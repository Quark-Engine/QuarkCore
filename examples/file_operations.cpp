#include "QuarkCore/QuarkCore.hpp"

int main() {
    qc::InitWindow(1280, 720, "QuarkCore File Operations Example", qc::RendererType::OpenGL);
    qc::SetLogLevel(qc::LogLevel::Info);

    const char* appDir = qc::GetApplicationDirectory();
    const char* workDir = qc::GetWorkingDirectory();

    const char* testDir = "test_data";
    qc::MakeDirectory(testDir);
    qc::MakeDirectory("test_data/subdirectory");

    qc::FilePathList files = qc::LoadDirectoryFiles(".");
    qc::FilePathList droppedFiles{};
    bool hasDropped = false;

    const char* testNames[] = { "valid_file.txt", "<invalid>.txt", "con", "file|name.txt" };
    bool testNamesValid[4];
    for (int i = 0; i < 4; ++i)
        testNamesValid[i] = qc::IsFileNameValid(testNames[i]);

    while (!qc::WindowShouldClose()) {
        if (qc::IsFileDropped()) {
            if (hasDropped) qc::UnloadDroppedFiles(droppedFiles);
            droppedFiles = qc::LoadDroppedFiles();
            hasDropped = true;
        }

        if (qc::IsKeyPressed(qc::KeyboardKey::Space)) {
            qc::UnloadDirectoryFiles(files);
            files = qc::LoadDirectoryFiles(".");
        }

        qc::BeginDrawing();
        qc::ClearBackground(qc::Color{20, 24, 32, 255});

        int y = 20;

        qc::DrawText("QuarkCore File Operations Example", 20, y, 32, qc::YELLOW);
        y += 50;

        qc::DrawText(qc::TextFormat("App Directory: %s", appDir), 20, y, 20, qc::WHITE);
        y += 28;

        qc::DrawText(qc::TextFormat("Working Directory: %s", workDir), 20, y, 20, qc::WHITE);
        y += 28;

        qc::DrawText(qc::TextFormat("Files in current directory: %d", files.count), 20, y, 20, qc::WHITE);
        y += 28;

        for (unsigned int i = 0; i < files.count && i < 5; ++i) {
            qc::DrawText(qc::TextFormat("  %s", files.paths[i]), 20, y, 18, qc::LIGHTGRAY);
            y += 22;
        }
        if (files.count > 5) {
            qc::DrawText(qc::TextFormat("  ... and %d more", files.count - 5), 20, y, 18, qc::GRAY);
            y += 22;
        }
        y += 10;

        qc::DrawText("Filename Validation:", 20, y, 20, qc::WHITE);
        y += 26;
        for (int i = 0; i < 4; ++i) {
            qc::DrawText(
                qc::TextFormat("  %s  ->  %s", testNames[i], testNamesValid[i] ? "valid" : "invalid"),
                20, y, 18, testNamesValid[i] ? qc::GREEN : qc::RED
            );
            y += 22;
        }
        y += 10;

        qc::DrawText(qc::TextFormat("test_data/ exists: %s", qc::DirectoryExists(testDir) ? "yes" : "no"), 20, y, 20, qc::SKYBLUE);
        y += 28;

        qc::DrawText(qc::TextFormat("Parent directory: %s", qc::GetPrevDirectoryPath(workDir)), 20, y, 20, qc::WHITE);
        y += 36;

        if (hasDropped) {
            qc::DrawText("Dropped Files:", 20, y, 20, qc::YELLOW);
            y += 26;
            for (unsigned int i = 0; i < droppedFiles.count; ++i) {
                const char* path = droppedFiles.paths[i];
                qc::DrawText(qc::TextFormat("  %s", path), 20, y, 18, qc::LIGHTGRAY);
                y += 22;
                qc::DrawText(qc::TextFormat("    size: %d bytes", qc::GetFileLength(path)), 20, y, 16, qc::GRAY);
                y += 20;
                qc::DrawText(qc::TextFormat("    ext:  %s", qc::GetFileExtension(path)), 20, y, 16, qc::GRAY);
                y += 20;
                qc::DrawText(qc::TextFormat("    name: %s", qc::GetFileName(path)), 20, y, 16, qc::GRAY);
                y += 24;
            }
        } 
        
        else {
            qc::DrawText("Drag and drop files here to inspect them", 20, y, 20, qc::GRAY);
            y += 28;
        }

        qc::DrawText("Press SPACE to reload directory listing", 20, y, 18, qc::DARKGRAY);

        qc::EndDrawing();
    }

    qc::UnloadDirectoryFiles(files);
    if (hasDropped) qc::UnloadDroppedFiles(droppedFiles);
    qc::CloseWindow();

    return 0;
}