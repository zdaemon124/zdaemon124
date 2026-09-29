#include "EditorApp.h"

int main(int argc, char** argv)
{
    ze::ApplicationDesc desc;
    desc.window.title = "ZEngine Editor";
    desc.window.width = 1600;
    desc.window.height = 900;
    ze::EditorApp app(desc, argc, argv);
    app.Run();
    return 0;
}
