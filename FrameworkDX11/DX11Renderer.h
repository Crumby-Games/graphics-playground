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

// DX 11 Renderer class for encapsulating the responsibily of rendering, or calling the render methods of renderable objects

#pragma once

#include <d3d11_1.h>
#include "constants.h"
#include "Camera.h"
#include "wrl.h"
#include "structures.h"
#include <vector>

class Scene;


struct ImGuiParameterState
{
	int selected_radio;
};

class DX11Renderer
{
public:
	DX11Renderer() = default;
	~DX11Renderer() = default;

	HRESULT init(HWND hwnd);
	void	cleanUp();

	void	update(const float deltaTime);

	// a helper method - todo: move to a unique class to reduce dependency on Renderer
	static HRESULT compileShaderFromFile(const WCHAR* szFileName, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut);

	void input(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private: // methods
	HRESULT initDevice(HWND hwnd);
	void    cleanupDevice();
	void	initIMGUI(HWND hwnd);
	void	startIMGUIDraw(const unsigned int FPS);
	void	completeIMGUIDraw();
	void	CentreMouseInWindow(HWND hWnd);


public: // properties

	D3D_DRIVER_TYPE									m_driverType = D3D_DRIVER_TYPE_NULL;
	D3D_FEATURE_LEVEL								m_featureLevel = D3D_FEATURE_LEVEL_11_0;
	Microsoft::WRL::ComPtr <ID3D11Device>			m_pd3dDevice;
	Microsoft::WRL::ComPtr <ID3D11Device1>			m_pd3dDevice1;
	Microsoft::WRL::ComPtr <ID3D11DeviceContext>	m_pImmediateContext;
	Microsoft::WRL::ComPtr <ID3D11DeviceContext1>	m_pImmediateContext1;
	Microsoft::WRL::ComPtr <IDXGISwapChain>			m_pSwapChain;
	Microsoft::WRL::ComPtr <IDXGISwapChain1>		m_pSwapChain1;
	Microsoft::WRL::ComPtr <ID3D11RenderTargetView> m_pRenderTargetView;
	Microsoft::WRL::ComPtr <ID3D11Texture2D>		m_pDepthStencil;
	Microsoft::WRL::ComPtr <ID3D11DepthStencilView> m_pDepthStencilView;

	Microsoft::WRL::ComPtr <ID3D11VertexShader>		m_pVertexShader;
	Microsoft::WRL::ComPtr <ID3D11PixelShader>		m_pPixelShader;
	Microsoft::WRL::ComPtr <ID3D11InputLayout>		m_pVertexLayout;

	XMFLOAT4X4				m_matProjection;
	ConstantBuffer			m_ConstantBufferData;


	Scene* m_pScene;
	

};

