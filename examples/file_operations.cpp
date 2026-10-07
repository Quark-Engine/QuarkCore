#include "QuarkCore/QuarkCore.hpp"

int main() {
    InitWindow(1280, 720, "QuarkCore File Operations Example", RendererType::OpenGL);
    SetLogLevel(LogLevel::Info);

    const char* appDir = GetApplicationDirectory();
    const char* workDir = GetWorkingDirectory();

    const char* testDir = "test_data";
    MakeDirectory(testDir);
    MakeDirectory("test_data/subdirectory");

    FilePathList files = LoadDirectoryFiles(".");
    FilePathList droppedFiles{};
    bool hasDropped = false;

    const char* testNames[] = { "valid_file.txt", "<invalid>.txt", "con", "file|name.txt" };
    bool testNamesValid[4];
    for (int i = 0; i < 4; ++i)
        testNamesValid[i] = IsFileNameValid(testNames[i]);

    while (!WindowShouldClose()) {
        if (IsFileDropped()) {
            if (hasDropped) UnloadDroppedFiles(droppedFiles);
            droppedFiles = LoadDroppedFiles();
            hasDropped = true;
        }

        if (IsKeyPressed(KEY_SPACE)) {
            UnloadDirectoryFiles(files);
            files = LoadDirectoryFiles(".");
        }

        BeginDrawing();
        ClearBackground(Color{20, 24, 32, 255});

        int y = 20;

        DrawText("QuarkCore File Operations Example", 20, y, 32, YELLOW);
        y += 50;

        DrawText(TextFormat("App Directory: %s", appDir), 20, y, 20, WHITE);
        y += 28;

        DrawText(TextFormat("Working Directory: %s", workDir), 20, y, 20, WHITE);
        y += 28;

        DrawText(TextFormat("Files in current directory: %d", files.count), 20, y, 20, WHITE);
        y += 28;

        for (unsigned int i = 0; i < files.count && i < 5; ++i) {
            DrawText(TextFormat("  %s", files.paths[i]), 20, y, 18, LIGHTGRAY);
            y += 22;
        }
        if (files.count > 5) {
            DrawText(TextFormat("  ... and %d more", files.count - 5), 20, y, 18, GRAY);
            y += 22;
        }
        y += 10;

        DrawText("Filename Validation:", 20, y, 20, WHITE);
        y += 26;
        for (int i = 0; i < 4; ++i) {
            DrawText(
                TextFormat("  %s  ->  %s", testNames[i], testNamesValid[i] ? "valid" : "invalid"),
                20, y, 18, testNamesValid[i] ? GREEN : RED
            );
            y += 22;
        }
        y += 10;

        DrawText(TextFormat("test_data/ exists: %s", DirectoryExists(testDir) ? "yes" : "no"), 20, y, 20, SKYBLUE);
        y += 28;

        DrawText(TextFormat("Parent directory: %s", GetPrevDirectoryPath(workDir)), 20, y, 20, WHITE);
        y += 36;

        if (hasDropped) {
            DrawText("Dropped Files:", 20, y, 20, YELLOW);
            y += 26;
            for (unsigned int i = 0; i < droppedFiles.count; ++i) {
                const char* path = droppedFiles.paths[i];
                DrawText(TextFormat("  %s", path), 20, y, 18, LIGHTGRAY);
                y += 22;
                DrawText(TextFormat("    size: %d bytes", GetFileLength(path)), 20, y, 16, GRAY);
                y += 20;
                DrawText(TextFormat("    ext:  %s", GetFileExtension(path)), 20, y, 16, GRAY);
                y += 20;
                DrawText(TextFormat("    name: %s", GetFileName(path)), 20, y, 16, GRAY);
                y += 24;
            }
        } 
        
        else {
            DrawText("Drag and drop files here to inspect them", 20, y, 20, GRAY);
            y += 28;
        }

        DrawText("Press SPACE to reload directory listing", 20, y, 18, DARKGRAY);

        EndDrawing();
    }

    UnloadDirectoryFiles(files);
    if (hasDropped) UnloadDroppedFiles(droppedFiles);
    CloseWindow();

    return 0;
}