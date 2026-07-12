//****************************************************************************
//
//  File:       Packing.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   Implementation of RollingBufferAllocator
//
//****************************************************************************

#include "Packing.h"

#include "GraphicsDevice.h"

namespace GLVU
{

//****************************************************************************
//
//  Function:   RollingBufferAllocator::RollingBufferAllocator
//
//  Purpose:    Construct, needs the device for scratch buffer access.
//
//****************************************************************************
RollingBufferAllocator::RollingBufferAllocator(GraphicsDevice* device) : 
	device_(device)
{
	Restart();
}

//****************************************************************************
//
//  Function:   RollingBufferAllocator::Allocate
//
//  Purpose:    Attempts to aquire allocation space for write into a GPU buffer
//
//  Return:     Allocation target as <address, offset>
//
//****************************************************************************
std::pair<void*, size_t> RollingBufferAllocator::Allocate(size_t dataSize)
{
	return AllocateInternal(dataSize, true);
}

//****************************************************************************
//
//  Function:   RollingBufferAllocator::AllocateInternal
//
//  Purpose:    Handles the two tries at allocating GPU memory access.
//
//  Return:     Allocation target as <address, offset>
//
//****************************************************************************
std::pair<void*, size_t> RollingBufferAllocator::AllocateInternal(size_t dataSize, bool firstTry)
{
	auto allocation = workingPool_.Allocate(dataSize);
	if (allocation.first != nullptr)
	{
		records_.push_back({ allocation.second, dataSize, nullptr });
		return allocation;
	}
	else if (!firstTry)
	{
		// raise an error
		device_->LogFormat(GLVU_ERROR, "RollingBufferAllocator failure allocating: %zu bytes", dataSize);
		return { nullptr, 0 };
	}

	Finish();
	Restart();

	return AllocateInternal(dataSize, false);
}

//****************************************************************************
//
//  Function:   RollingBufferAllocator::Finish
//
//  Purpose:    Commits data to the GPU and sets the buffer object in the local
//				allocation records that are to be read for glBindBufferRange
//
//****************************************************************************
void RollingBufferAllocator::Finish()
{
	if (workingPool_.totalSize_ > 0)
	{
		auto buff = device_->GetScratchUniformBuffer(device_->GPU_MaxUBOSize());
		workingPool_.Transfer(buff);
		ApplyBuffer(buff);
	}
}

//****************************************************************************
//
//  Function:   CLEX_GetNumber
//
//  Purpose:    Reliably fetch a number from the lexer, is potentially an ancient
//              value if used when an int/float is not ready.
//
//  Return:     Value as floating point
//
//****************************************************************************
void RollingBufferAllocator::Restart()
{
	workingPool_ = BufferPool(device_->GPU_MaxUBOSize(), device_->GPU_MinUBOAlignment());
}

//****************************************************************************
//
//  Function:   RollingBufferAllocator::ApplyBuffer
//
//  Purpose:    Any record with a null buffer will have it's value set to the
//				given buffer. It's handled this way so that pulling buffers
//				from the scratch pool is only done when actually needed as
//				an algorithm may be much easier if you don't have to perform
//				special checks for the 0/null case.
//
//				ie. the *I have no work to do case* assessment may be just as
//				heavy as traversing the possibility space of the potential work
//				required.
//
//****************************************************************************
void RollingBufferAllocator::ApplyBuffer(std::shared_ptr<Buffer> buffer)
{
	for (auto& rec : records_)
		if (rec.buffer_ == nullptr)
			rec.buffer_ = buffer;
}



}
