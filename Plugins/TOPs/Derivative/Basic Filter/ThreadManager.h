/* Shared Use License: This file is owned by Derivative Inc. (Derivative)
* and can only be used, and/or modified for use, in conjunction with
* Derivative's TouchDesigner software, and only if you are a licensee who has
* accepted Derivative's TouchDesigner license or assignment agreement
* (which also govern the use of this file). You may share or redistribute
* a modified version of this file provided the following conditions are met:
*
* 1. The shared file or redistribution must retain the information set out
* above and this list of conditions.
* 2. Derivative's name (Derivative Inc.) or its trademarks may not be used
* to endorse or promote products derived from this file without specific
* prior written permission from Derivative.
*/

#ifndef __ThreadManager__
#define __ThreadManager__

#include "TOP_CPlusPlusBase.h"

#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>

using namespace TD;

// class Parameters;
class OP_Inputs;


enum class ThreadStatus
{
	Busy,
	Ready,
	Done,
	Waiting
};

class ThreadManager
{
public:
	ThreadManager();

	~ThreadManager();

	void	sync(
		bool doDither,
		int bitsPerColor,
		int inWidth,
		int inHeight,
		const TD::OP_TextureDesc& outputDesc,
		const OP_SmartRef<OP_TOPDownloadResult> downRes,
		TD::TOP_Context* context);


	void	popOutBuffer(OP_SmartRef<TOP_Buffer>& outBuffer, TD::TOP_UploadInfo& info);
	
	ThreadStatus getStatus();
private:
	void	threadFn();

	std::atomic<ThreadStatus>			myStatus;

	// Out buffer resource
	OP_SmartRef<OP_TOPDownloadResult>	myDownRes;
	OP_SmartRef<TOP_Buffer>				myOutBuffer;

	// Thread and Sync variables
	std::thread*			myThread;
	std::mutex				myBufferMutex;
	std::condition_variable myBufferCV;
	std::atomic_bool		myThreadShouldExit;

	// Parameters saved
	int						myInWidth;
	int						myInHeight;
	int						myOutWidth;
	int						myOutHeight;
	bool					myDoDither;
	int						myBitsPerColor;
	TD::TOP_Context*		myContext;
	TD::TOP_UploadInfo		myUploadInfo;
};

#endif
