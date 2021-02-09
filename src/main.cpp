#include "pch.h"
#include "renderer.h"

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	Renderer *renderer = reinterpret_cast<Renderer *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if(!renderer) {
		return DefWindowProc(hwnd, msg, wparam, lparam);
	}

	ImGuiIO &io = ImGui::GetIO();
	switch (msg) {
	case WM_SIZE: {
		return DefWindowProc(hwnd, msg, wparam, lparam);
	} return 0;
	case WM_DESTROY: {
		PostQuitMessage(0);
	} return 0;
	case WM_MOUSEMOVE: {
		io.MousePos = ImVec2 {
			static_cast<float>(GET_X_LPARAM(lparam)),
			static_cast<float>(GET_Y_LPARAM(lparam))
		};
	} return 0;
	case WM_LBUTTONDOWN: {
		io.MouseDown[0] = true;
	} return 0;
	case WM_RBUTTONDOWN: {
		io.MouseDown[1] = true;
	} return 0;
	case WM_LBUTTONUP: {
		io.MouseDown[0] = false;
	} return 0;
	case WM_RBUTTONUP: {
		io.MouseDown[1] = false;
	} return 0;
	case WM_MOUSEWHEEL: {
		io.MouseWheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / 100.0f;
	} return 0;
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE prev_hinstance, 
	PWSTR cmd_line, int cmd_show) {
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);

	AllocConsole();
	FILE *dummy;
	freopen_s(&dummy, "CONIN$", "r", stdin);
	freopen_s(&dummy, "CONOUT$", "w", stdout);
	freopen_s(&dummy, "CONOUT$", "w", stderr);

	const wchar_t *window_class_name = L"VHR_Class";
	const wchar_t *window_title = L"Vulkan Hybrid Renderer";
	WNDCLASSEX window_class {
		.cbSize = sizeof(WNDCLASSEX),
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = WndProc,
		.hInstance = hinstance,
		.hCursor = LoadCursor(NULL, IDC_ARROW),
		.hbrBackground = nullptr,
		.lpszClassName = window_class_name,
	};

	if(!RegisterClassEx(&window_class)) {
		return 1;
	}

	HWND hwnd = CreateWindow(
		window_class_name,
		window_title,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		nullptr,
		nullptr,
		hinstance,
		nullptr
	);
	if(hwnd == NULL) return 1;
	ShowWindow(hwnd, cmd_show);

	Renderer renderer(hinstance, hwnd);
	SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&renderer));

	MSG msg;
	bool alive = true;
	while(alive) {
		while(PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if(msg.message == WM_QUIT) {
				alive = false;
				break;
			}
		}

		if(IsIconic(hwnd)) {
			continue;
		}
		else {
			renderer.Update();
			renderer.Present();
		}
	}

	UnregisterClass(window_class_name, hinstance);
	DestroyWindow(hwnd);
	return 0;
}
