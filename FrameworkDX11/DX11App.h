// MIT License
// Copyright (c) 2025 David White
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// The App class with responsibility for the lifetime of the project

#pragma once
#include "structures.h"
#include "wrl.h"

class DX11Renderer;

class DX11App
{
public: // methods
	DX11App();
	~DX11App();

	HRESULT		init();
	void		cleanUp();
	
	HRESULT		initWindow(HINSTANCE hInstance, int nCmdShow);
	
	void		update();
	float		calculateDeltaTime();
	DX11Renderer* getRenderer() { return m_pRenderer; }

private: // properties

	HINSTANCE	m_hInst = nullptr;
	HWND		m_hWnd = nullptr;

	int			m_viewWidth = 0;
	int			m_viewHeight = 0;

	DX11Renderer* m_pRenderer = nullptr;
};

