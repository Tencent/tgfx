#include "TextureDrawer.h"

#include <filesystem>
#include <QSGImageNode>
#include "Draw.h"

Q_DECLARE_METATYPE(std::shared_ptr<tgfx::Image>)
namespace inspector {
    static tgfx::Rect calcInerRect(const tgfx::Rect &rect, float aspectRatio) {
        float w = rect.width();
        float h = rect.height();
        const float paddingRatio = 0.05f;
        const float innerScaleRatio = 1-2*paddingRatio;
        if (w<=h*aspectRatio) {//外部矩形更扁
            float innerWidth = w * innerScaleRatio;
            float innerHeight = innerWidth / aspectRatio;
            float x = paddingRatio * w;
            float y = (h - innerHeight) / 2;
            return tgfx::Rect::MakeXYWH(x+rect.x(), y+rect.y(), innerWidth, innerHeight);
        } else {//外部矩形更瘦
            float innerHeight = h * innerScaleRatio;
            float innerWidth = innerHeight * aspectRatio;
            float x = (w - innerWidth) / 2;
            float y = paddingRatio * h;
            return tgfx::Rect::MakeXYWH(x+rect.x(), y+rect.y(), innerWidth, innerHeight);
        }
    }
    TextureDrawer::TextureDrawer(QQuickItem *parent):
    QQuickItem(parent),appHost(AppHostSingleton::GetInstance())
    {
        setFlag(ItemHasContents, true);
        qRegisterMetaType<std::shared_ptr<tgfx::Image>>();
    }

    void TextureDrawer::onSelectedImage(std::shared_ptr<tgfx::Image> image) {
        image = image->makeMipmapped(true);
        this->image = image;
    }

    void TextureDrawer::draw() {
            auto device = tgfxWindow->getDevice();
        if (device == nullptr) {
            return;
        }
        auto context = device->lockContext();
        if (context == nullptr) {
            return;
        }
        auto surface = tgfxWindow->getSurface(context);
        if (surface == nullptr) {
            device->unlock();
            return;
        }
        auto canvas = surface->getCanvas();
        canvas->clear();
        canvas->setMatrix(tgfx::Matrix::MakeScale(appHost->density(), appHost->density()));
        auto rect = tgfx::Rect::MakeXYWH(0.f,0.f,static_cast<float>(width()), static_cast<float>(height()));

        if (image)
            canvas->drawImageRect(image, calcInerRect(rect,image->width() / static_cast<float>(image->height())), tgfx::SamplingOptions(tgfx::FilterMode::Linear, tgfx::MipmapMode::Linear));

        context->flushAndSubmit();
        tgfxWindow->present(context);
        device->unlock();
    }


    QSGNode * TextureDrawer::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) {
        auto node = static_cast<QSGImageNode*>(oldNode);
        if (!tgfxWindow) {
            tgfxWindow = tgfx::QGLWindow::MakeFrom(this, true);
        }
        auto pixelRatio = window()->devicePixelRatio();
        auto screenWidth = static_cast<int>(ceil(width() * pixelRatio));
        auto screenHeight = static_cast<int>(ceil((float)height() * pixelRatio));
        auto sizeChanged = appHost->updateScreen(screenWidth, screenHeight, static_cast<float>(pixelRatio));
        if (sizeChanged) {
            tgfxWindow->invalidSize();
        }
        draw();
        auto texture = tgfxWindow->getQSGTexture();
        if (texture) {
            if (node == nullptr) {
                node = window()->createImageNode();
            }
            node->setTexture(texture);
            node->markDirty(QSGNode::DirtyMaterial);
            node->setRect(boundingRect());
        }
        return node;
    }
}
