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

// A simple camera - it's not really needed for this demo, but it quickly allows a fly about.

#pragma once

#include <DirectXMath.h>
#include <windows.h>
#include <windowsx.h>
#include "constants.h"

using namespace DirectX;

class Camera
{
public:
	Camera(XMFLOAT3 posIn, XMFLOAT3 lookDirIn, XMFLOAT3 upIn, const int width, const int height)
	{
		m_position = posIn;
		m_lookDir = lookDirIn;
		m_up = upIn;

		XMStoreFloat4x4(&m_viewMatrix, XMMatrixIdentity());

		// Initialize the projection matrix
		constexpr float fovAngleY = XMConvertToRadians(60.0f);
		XMStoreFloat4x4(&m_projectionMatrix, XMMatrixPerspectiveFovLH(fovAngleY, width / (FLOAT)height, 0.01f, 100.0f));
	}

	XMFLOAT3 getPosition() { return m_position; }

	void moveForward(float distance)
	{
		// Get the normalized forward vector (camera's look direction)
		XMVECTOR forwardVec = XMVector3Normalize(XMLoadFloat3(&m_lookDir));
		XMVECTOR posVec = XMLoadFloat3(&m_position);

		// Move in the direction the camera is facing
		XMStoreFloat3(&m_position, XMVectorMultiplyAdd(XMVectorReplicate(distance), forwardVec, posVec));
	}

	void strafeLeft(float distance)
	{
		// Get the current look direction and up vector
		XMVECTOR lookDirVec = XMLoadFloat3(&m_lookDir);
		XMVECTOR upVec = XMLoadFloat3(&m_up);

		// Calculate the right vector (side vector of the camera)
		XMVECTOR rightVec = XMVector3Normalize(XMVector3Cross(upVec, lookDirVec));
		XMVECTOR posVec = XMLoadFloat3(&m_position);

		// Move left by moving opposite to the right vector
		XMStoreFloat3(&m_position, XMVectorMultiplyAdd(XMVectorReplicate(-distance), rightVec, posVec));
	}

	void moveBackward(float distance)
	{
		// Call MoveForward with negative distance to move backward
		moveForward(-distance);
	}

	void strafeRight(float distance)
	{
		// Call StrafeLeft with negative distance to move right
		strafeLeft(-distance);
	}

	void updateLookAt(POINTS delta)
	{
		// Sensitivity factor for mouse movement
		const float sensitivity = 0.001f;

		// Apply sensitivity
		float dx = delta.x * sensitivity; // Yaw change
		float dy = delta.y * sensitivity; // Pitch change


		// Get the current look direction and up vector
		XMVECTOR lookDirVec = XMLoadFloat3(&m_lookDir);
		lookDirVec = XMVector3Normalize(lookDirVec);
		XMVECTOR upVec = XMLoadFloat3(&m_up);
		upVec = XMVector3Normalize(upVec);

		// Calculate the camera's right vector
		XMVECTOR rightVec = XMVector3Cross(upVec, lookDirVec);
		rightVec = XMVector3Normalize(rightVec);



		// Rotate the lookDir vector left or right based on the yaw
		lookDirVec = XMVector3Transform(lookDirVec, XMMatrixRotationAxis(upVec, dx));
		lookDirVec = XMVector3Normalize(lookDirVec);

		// Rotate the lookDir vector up or down based on the pitch
		lookDirVec = XMVector3Transform(lookDirVec, XMMatrixRotationAxis(rightVec, dy));
		lookDirVec = XMVector3Normalize(lookDirVec);


		// Re-calculate the right vector after the yaw rotation
		rightVec = XMVector3Cross(upVec, lookDirVec);
		rightVec = XMVector3Normalize(rightVec);

		// Re-orthogonalize the up vector to be perpendicular to the look direction and right vector
		upVec = XMVector3Cross(lookDirVec, rightVec);
		upVec = XMVector3Normalize(upVec);

		// Store the updated vectors back to the class members
		XMStoreFloat3(&m_lookDir, lookDirVec);
		XMStoreFloat3(&m_up, upVec);
	}

	void update() { updateViewMatrix(); }

	XMMATRIX getViewMatrix() const
	{
		updateViewMatrix();
		return XMLoadFloat4x4(&m_viewMatrix);
	}

	XMMATRIX getProjectionMatrix() const
	{
		return XMLoadFloat4x4(&m_projectionMatrix);
	}

private:
	void updateViewMatrix() const
	{
		// Calculate the look-at point based on the position and look direction
		XMVECTOR posVec = XMLoadFloat3(&m_position);
		XMVECTOR lookDirVec = XMLoadFloat3(&m_lookDir);
		XMVECTOR lookAtPoint = posVec + lookDirVec; // This is the new look-at point

		// Update the view matrix to look from the camera's position to the look-at point
		XMStoreFloat4x4(&m_viewMatrix, XMMatrixLookAtLH(posVec, lookAtPoint, XMLoadFloat3(&m_up)));
	}

	XMFLOAT3    m_position;
	XMFLOAT3	m_lookDir;
	XMFLOAT3	m_up;
	XMFLOAT4X4	m_projectionMatrix;
	mutable XMFLOAT4X4 m_viewMatrix;
};

