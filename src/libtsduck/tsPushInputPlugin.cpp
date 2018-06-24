//----------------------------------------------------------------------------
//
// TSDuck - The MPEG Transport Stream Toolkit
// Copyright (c) 2005-2018, Thierry Lelegard
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.
//
//----------------------------------------------------------------------------

#include "tsPushInputPlugin.h"
TSDUCK_SOURCE;


//----------------------------------------------------------------------------
// Constructors and destructors.
//----------------------------------------------------------------------------

ts::PushInputPlugin::PushInputPlugin(TSP* tsp_,
                                     const UString& description,
                                     const UString& syntax,
                                     const UString& help) :
    InputPlugin(tsp_, description, syntax, help),
    _receiver(tsp_, this),
    _started(false),
    _queue()
{
}

ts::PushInputPlugin::Receiver::Receiver(TSP* tsp_, PushInputPlugin* plugin) :
    _tsp(tsp_),
    _plugin(plugin)
{
}

ts::PushInputPlugin::Receiver::~Receiver()
{
    waitForTermination();
}


//----------------------------------------------------------------------------
// Tune the TS packet buffer.
//----------------------------------------------------------------------------

void ts::PushInputPlugin::setQueueSize(size_t count)
{
    _queue.reset(count);
}


//----------------------------------------------------------------------------
// Internal thread which receives TS packets.
//----------------------------------------------------------------------------

void ts::PushInputPlugin::Receiver::main()
{
    _tsp->debug(u"internal push-input thread started");

    // Simply let the subclass perform input until the end.
    _plugin->processInput();

    // Push an end of file mark.
    _plugin->_queue.setEOF();

    _tsp->debug(u"internal push-input thread completed");
}


//----------------------------------------------------------------------------
// Start method
//----------------------------------------------------------------------------

bool ts::PushInputPlugin::start()
{
    // Nothing to do in this version.
    return true;
}


//----------------------------------------------------------------------------
// Stop method
//----------------------------------------------------------------------------

bool ts::PushInputPlugin::stop()
{
    // Send the stop condition to the internal packet queue.
    _queue.stop();

    // Wait for received thread termination.
    _receiver.waitForTermination();
    return true;
}


//----------------------------------------------------------------------------
// Standard input routine, now hidden from subclasses.
//----------------------------------------------------------------------------

size_t ts::PushInputPlugin::receive(TSPacket* buffer, size_t max_packets)
{
    // Start the receiver thread the first time.
    if (!_started) {
        _receiver.setAttributes(ThreadAttributes().setStackSize(stackUsage()));
        if (!_receiver.start()) {
            return false;
        }
        _started = true;
    }

    size_t count = 0;
    BitRate bitrate = 0;

    // Wait for some packets from the receiver thread.
    if (!_queue.waitPackets(buffer, max_packets, count, bitrate)) {
        // End of input.
        count = 0;
    }

    assert(count <= max_packets);
    return count;
}


//----------------------------------------------------------------------------
// Push packet to the tsp chain.
//----------------------------------------------------------------------------

bool ts::PushInputPlugin::pushPackets(const TSPacket* buffer, size_t count)
{
    // We are executing in the context of the receiver thread.
    // Send packets by chunks, loop until everything is pushed.
    while (count > 0) {

        TSPacket* out_buffer = 0;
        size_t out_count = 0;

        // Wait for space in the queue buffer.
        // Abort now if the application is terminating.
        if (tsp->aborting() || _queue.stopped() || !_queue.lockWriteBuffer(out_buffer, out_count, count)) {
            return false;
        }

        assert(out_buffer != 0);
        assert(out_count > 0);

        // Move packets into the queue.
        if (out_count > count) {
            out_count = count;
        }
        TSPacket::Copy(out_buffer, buffer, out_count);
        buffer += out_count;
        count -= out_count;

        // Signal the new packets in the queue.
        _queue.releaseWriteBuffer(out_count);
    }

    return true;
}
