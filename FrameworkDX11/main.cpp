//--------------------------------------------------------------------------------------
// File: main.cpp
//

#define _XM_NO_INTRINSICS_ // no platform specific intrinsics

#include "main.h"
#include "constants.h"
#include "Camera.h"
#include "DX11App.h"

DX11App app;


//--------------------------------------------------------------------------------------
// Entry point to the program. Initializes everything and goes into a message processing 
// loop. Idle time is used to render the scene.
//--------------------------------------------------------------------------------------
int WINAPI wWinMain( _In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow )
{
    UNREFERENCED_PARAMETER( hPrevInstance );
    UNREFERENCED_PARAMETER( lpCmdLine );

    if( FAILED(app.initWindow( hInstance, nCmdShow ) ) )
        return 0;

    if( FAILED(app.init() ) )
    {
        app.cleanUp();
        return 0;
    }

    // Main message loop
    MSG msg = {0};
    while( WM_QUIT != msg.message )
    {
        if( PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) )
        {
            TranslateMessage( &msg );
            DispatchMessage( &msg );
        }
        else
        {
            app.update();
        }
    }

    app.cleanUp();

    return ( int )msg.wParam;
}







