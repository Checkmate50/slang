// shader.slang

struct ioStruct { float data; };

//TEST_INPUT:ubuffer(random(float, 4096, -1.0, 1.0), stride=4):name=ioBuffer
RWStructuredBuffer<ioStruct> ioBuffer;

[shader("compute")]
[numthreads(4, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint tid = dispatchThreadID.x;

    float i = ioBuffer[tid].data;
    float o = i < 0.5 ? (i * .5) : sqrt(i);

    ioBuffer[tid].data = o;
}
