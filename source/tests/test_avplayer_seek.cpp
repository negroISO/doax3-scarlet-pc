// Restart a real MP4 through the production guest-file adapter and demuxer seek.
#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "core/libraries/avplayer/avplayer_file_streamer.h"
#include "core/libraries/avplayer/avplayer_seek.h"
using namespace Libraries::AvPlayer;
struct File { std::vector<u8> bytes; };
static s32 PS4_SYSV_ABI Open(void*, const char*) { return 0; }
static s32 PS4_SYSV_ABI Close(void*) { return 0; }
static s32 PS4_SYSV_ABI Read(void* opaque, u8* out, u64 offset, u32 size) {
    const auto& bytes=static_cast<File*>(opaque)->bytes;
    if (offset>=bytes.size()) return 0;
    size=std::min<u64>(size,bytes.size()-offset);
    std::memcpy(out,bytes.data()+offset,size);
    return size;
}
static u64 PS4_SYSV_ABI Size(void* opaque) { return static_cast<File*>(opaque)->bytes.size(); }
int main(int argc, char** argv) {
    if (argc!=2) { std::fprintf(stderr,"Usage: avplayer-seek-test <mp4 fixture>\n"); return 2; }
    FILE* input=std::fopen(argv[1],"rb"); assert(input);
    std::fseek(input,0,SEEK_END); const long size=std::ftell(input); assert(size>0); std::rewind(input);
    File file; file.bytes.resize(size);
    assert(std::fread(file.bytes.data(),1,size,input)==static_cast<size_t>(size));
    std::fclose(input);
    AvPlayerFileStreamer adapter({&file,Open,Close,Read,Size});
    assert(adapter.Init("fixture.mp4"));
    AVFormatContext* context=avformat_alloc_context(); assert(context);
    context->pb=adapter.GetContext(); context->flags|=AVFMT_FLAG_CUSTOM_IO;
    assert(avformat_open_input(&context,nullptr,nullptr,nullptr)==0);
    assert(avformat_find_stream_info(context,nullptr)>=0);
    AVPacket* packet=av_packet_alloc(); assert(packet);
    for(int loop=0; loop<4; ++loop) {
        int count=0, result;
        while((result=av_read_frame(context,packet))>=0) { ++count; av_packet_unref(packet); }
        assert(count>0 && result==AVERROR_EOF);
        assert(RewindDemuxer(context)>=0);
        assert(av_read_frame(context,packet)>=0);
        assert(packet->pts==0);
        av_packet_unref(packet);
    }
    av_packet_free(&packet); avformat_close_input(&context);
    std::puts("AvPlayer MP4 loop seek: four restarts passed");
}
