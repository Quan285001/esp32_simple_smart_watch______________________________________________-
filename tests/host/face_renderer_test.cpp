#include "face_renderer.h"
#include "display_canvas.h"

#include <assert.h>
#include <string>
#include <vector>

class TestCanvas final : public DisplayCanvas {
public:
    std::vector<std::string> calls;
    int clipCalls = 0;
    int resetCalls = 0;
    int clipX = -1, clipY = -1, clipW = -1, clipH = -1;

    uint16_t width() const override { return 240; }
    uint16_t height() const override { return 240; }
    void clearScreen(uint16_t) override { calls.push_back("clear"); }
    void fillRect(int, int, int, int, uint16_t) override { calls.push_back("fill"); }
    void drawRect(int, int, int, int, uint16_t) override { calls.push_back("rect"); }
    void fillRoundRect(int, int, int, int, int, uint16_t) override { calls.push_back("round-fill"); }
    void drawRoundRect(int, int, int, int, int, uint16_t) override { calls.push_back("round"); }
    void fillCircle(int, int, int, uint16_t) override { calls.push_back("circle-fill"); }
    void drawCircle(int, int, int, uint16_t) override { calls.push_back("circle"); }
    void drawLine(int, int, int, int, uint16_t) override { calls.push_back("line"); }
    void pushRgb565Row(int, int, uint16_t, const uint16_t*) override { calls.push_back("image-row"); }
    void setClipRect(int x, int y, int w, int h) override {
        ++clipCalls; clipX=x; clipY=y; clipW=w; clipH=h; calls.push_back("clip");
    }
    void resetClip() override { ++resetCalls; calls.push_back("reset-clip"); }
    void drawText(const char* text, int, int, int, int, uint16_t, float, float, float,
                  uint16_t, float, CanvasTextAlignment) override {
        calls.push_back(std::string("text:") + text);
    }
    void reset() { calls.clear(); clipCalls=0; resetCalls=0; clipX=clipY=clipW=clipH=-1; }
};

static TestCanvas canvas;
DisplayCanvas& getDisplayCanvas() { return canvas; }
bool drawImageElement(const FaceModel&, const FaceObject&) { canvas.calls.push_back("image"); return true; }

static FaceObject baseObject(FaceElementType type, const char* id) {
    FaceObject object{};
    object.type=type;
    object.visible=true;
    object.transform.x=20;
    object.transform.y=20;
    object.transform.width=100;
    object.transform.height=30;
    object.transform.opacity=1.0f;
    object.fontSize=16.0f;
    object.glyphScaleX=1.0f;
    object.glyphScaleY=1.0f;
    object.fontWeight=400;
    object.textColor=0xFFFF;
    object.fillColor=0x07E0;
    object.shapeKind=0;
    object.textAlign=FaceTextAlign::Center;
    for (size_t i=0; id[i] && i+1<sizeof(object.id); ++i) object.id[i]=id[i];
    return object;
}

static FaceModel baseFace() {
    FaceModel face{};
    face.canvasWidth=240;
    face.canvasHeight=240;
    face.backgroundColor=0x0000;
    return face;
}

int main() {
    struct tm timeInfo{};
    timeInfo.tm_hour=13; timeInfo.tm_min=5; timeInfo.tm_sec=9;
    timeInfo.tm_wday=0; timeInfo.tm_mday=8; timeInfo.tm_mon=10; timeInfo.tm_year=126;
    WeatherData weather{};

    FaceModel face=baseFace();
    face.objectCount=1;
    face.objects[0]=baseObject(FaceElementType::DigitalClock,"clock");
    face.objects[0].dataFormat[0]='H'; face.objects[0].dataFormat[1]='H';
    face.objects[0].dataFormat[2]=':'; face.objects[0].dataFormat[3]='m';
    face.objects[0].dataFormat[4]='m'; face.objects[0].dataFormat[5]='\0';
    face.objects[0].timeFormat[0]='1'; face.objects[0].timeFormat[1]='2';
    face.objects[0].timeFormat[2]='h'; face.objects[0].timeFormat[3]='\0';
    face.objects[0].showSeconds=true;
    canvas.reset();
    assert(renderWatchFace(face,timeInfo,weather)==FaceRenderStatus::Ok);
    assert(canvas.calls.size()==2 && canvas.calls[0]=="clear" && canvas.calls[1]=="text:01:05:09 PM");

    face.objects[0]=baseObject(FaceElementType::Date,"date");
    const char* dateFormat="ddd, DD MMM YYYY";
    for(size_t i=0;dateFormat[i]&&i+1<sizeof(face.objects[0].dataFormat);++i)face.objects[0].dataFormat[i]=dateFormat[i];
    canvas.reset();
    assert(renderWatchFace(face,timeInfo,weather)==FaceRenderStatus::Ok);
    assert(canvas.calls.back()=="text:SUN, 08 NOV 2026");

    face=baseFace(); face.objectCount=2;
    face.objects[0]=baseObject(FaceElementType::Shape,"top");
    face.objects[0].transform.zIndex=10;
    face.objects[1]=baseObject(FaceElementType::Text,"bottom");
    for(size_t i=0;i<5;++i)face.objects[1].text[i]="Hello"[i];
    canvas.reset();
    assert(renderWatchFace(face,timeInfo,weather)==FaceRenderStatus::Ok);
    assert(canvas.calls.size()==3);
    assert(canvas.calls[0]=="clear" && canvas.calls[1]=="text:Hello" && canvas.calls[2]=="fill");

    face.objects[1].transform.opacity=0.5f;
    assert(validateFaceModelForRenderer(face)==FaceRenderStatus::UnsupportedTransform);
    face.objects[1].transform.opacity=1.0f;
    face.objects[1].transform.rotation=90.0f;
    assert(validateFaceModelForRenderer(face)==FaceRenderStatus::UnsupportedTransform);
    face.objects[1].transform.rotation=0.0f;
    face.objects[1].fontFamily[0]='C'; face.objects[1].fontFamily[1]='o';
    face.objects[1].fontFamily[2]='m'; face.objects[1].fontFamily[3]='i';
    face.objects[1].fontFamily[4]='c'; face.objects[1].fontFamily[5]='\0';
    assert(validateFaceModelForRenderer(face)==FaceRenderStatus::UnsupportedFont);

    face=baseFace(); face.objectCount=3;
    face.objects[0]=baseObject(FaceElementType::Shape,"overlap");
    face.objects[0].transform.x=55; face.objects[0].transform.y=55;
    face.objects[0].transform.width=20; face.objects[0].transform.height=20;
    face.objects[0].transform.zIndex=0;
    face.objects[1]=baseObject(FaceElementType::DigitalClock,"dynamic");
    face.objects[1].transform.x=50; face.objects[1].transform.y=50;
    face.objects[1].transform.width=100; face.objects[1].transform.height=30;
    face.objects[1].transform.zIndex=1;
    face.objects[1].dataFormat[0]='H'; face.objects[1].dataFormat[1]='H';
    face.objects[1].dataFormat[2]=':'; face.objects[1].dataFormat[3]='m';
    face.objects[1].dataFormat[4]='m';
    face.objects[2]=baseObject(FaceElementType::Shape,"outside");
    face.objects[2].transform.x=0; face.objects[2].transform.y=0;
    face.objects[2].transform.width=10; face.objects[2].transform.height=10;
    canvas.reset();
    assert(renderWatchFaceDynamic(face,timeInfo,weather)==FaceRenderStatus::Ok);
    assert(canvas.clipCalls==1 && canvas.resetCalls==1);
    assert(canvas.calls.size()==4);
    assert(canvas.calls[0]=="clip" && canvas.calls[1]=="fill" &&
           canvas.calls[2]=="fill" && canvas.calls[3]=="text:13:05");
    assert(canvas.clipX==50 && canvas.clipY==50 && canvas.clipW==100 && canvas.clipH==30);

    std::cout << "WFS Face renderer host tests passed.\n";
    return 0;
}
