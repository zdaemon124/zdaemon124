#include "EditorApp.h"

int main(int argc, char** argv)
{
    ie::ApplicationDesc desc;
    desc.window.title = "IndeetsEngine Editor";
    desc.window.width = 1600;
    desc.window.height = 900;
    ie::EditorApp app(desc, argc, argv);
    app.Run();
    return 0;
}
