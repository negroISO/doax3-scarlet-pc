// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
extern "C" {
#include <libavformat/avformat.h>
}

namespace Libraries::AvPlayer {
// Let the demuxer reset its packet/sample cursors as well as the byte stream.
// Rewinding AVIO first leaves MOV at EOF even when its subsequent seek returns 0.
inline int RewindDemuxer(AVFormatContext* context) {
    const int64_t start = context->start_time == AV_NOPTS_VALUE ? 0 : context->start_time;
    return avformat_seek_file(context, -1, INT64_MIN, start, INT64_MAX, 0);
}
} // namespace Libraries::AvPlayer
