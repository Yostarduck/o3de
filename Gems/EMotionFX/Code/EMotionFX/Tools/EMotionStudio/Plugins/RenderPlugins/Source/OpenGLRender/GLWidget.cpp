/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

// include the required headers
#include "GLWidget.h"
#include "OpenGLRenderPlugin.h"
#include <EMotionFX/Rendering/Common/OrbitCamera.h>
#include <EMotionFX/Rendering/Common/OrthographicCamera.h>
#include <EMotionFX/Rendering/Common/FirstPersonCamera.h>
#include "../../../../EMStudioSDK/Source/EMStudioCore.h"
#include <EMotionFX/CommandSystem/Source/SelectionCommands.h>
#include <EMotionFX/Source/AnimGraphManager.h>
#include <EMotionFX/Source/Recorder.h>
#include "../../../../EMStudioSDK/Source/RenderPlugin/RenderViewWidget.h"


namespace EMStudio
{
    // constructor
    GLWidget::GLWidget(RenderViewWidget* parentWidget, OpenGLRenderPlugin* parentPlugin)
        : QOpenGLWidget(parentWidget)
        , RenderWidget(parentPlugin, parentWidget)
    {
        mParentRenderPlugin = parentPlugin;

        // construct the font metrics used for overlay text rendering
        mFont.setPointSize(10);
        mFontMetrics = new QFontMetrics(mFont);

        // create our default camera
        SwitchCamera(CAMMODE_ORBIT);

        // setup to get focus when we click or use the mouse wheel
        setFocusPolicy((Qt::FocusPolicy)(Qt::ClickFocus | Qt::WheelFocus));
        setMouseTracking(true);

        setAutoFillBackground(false);
    }


    // destructor
    GLWidget::~GLWidget()
    {
        // destruct the font metrics used for overlay text rendering
        delete mFontMetrics;
    }


    // initialize the Qt OpenGL widget (overloaded from the widget base class)
    void GLWidget::initializeGL()
    {
        // initializeOpenGLFunctions() and mParentRenderPlugin->InitializeGraphicsManager must be called first to ensure
        // all OpenGL functions have been resolved before doing anything that could make GL calls (e.g. resizing)
        initializeOpenGLFunctions();
        mParentRenderPlugin->InitializeGraphicsManager();
        if (mParentRenderPlugin->GetGraphicsManager())
        {
            mParentRenderPlugin->GetGraphicsManager()->SetGBuffer(&mGBuffer);
        }

        // set minimum render view dimensions
        setMinimumHeight(100);
        setMinimumWidth(100);

        mPerfTimer.StampAndGetDeltaTimeInSeconds();
    }


    // resize the Qt OpenGL widget (overloaded from the widget base class)
    void GLWidget::resizeGL(int width, int height)
    {
        // don't resize in case the render widget is hidden
        if (isHidden())
        {
            return;
        }

        mParentRenderPlugin->GetRenderUtil()->Validate();

        mWidth  = width;
        mHeight = height;
        mGBuffer.Resize(width, height);

        RenderGL::GraphicsManager* graphicsManager = mParentRenderPlugin->GetGraphicsManager();
        if (graphicsManager == nullptr || mCamera == nullptr)
        {
            return;
        }
    }


    // trigger a render
    void GLWidget::Render()
    {
        update();
    }


    // render frame
    void GLWidget::paintGL()
    {
        //SetVSync(false);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // don't render in case the render widget is hidden
        if (isHidden())
        {
            return;
        }

        mRenderTimer.Stamp();

        // render the scene
        RenderGL::GraphicsManager* graphicsManager = mParentRenderPlugin->GetGraphicsManager();
        if (graphicsManager == nullptr || mCamera == nullptr)
        {
            return;
        }

        painter.beginNativePainting();

        graphicsManager->SetGBuffer(&mGBuffer);

        RenderOptions* renderOptions = mParentRenderPlugin->GetRenderOptions();

        // get a pointer to the render utility
        RenderGL::GLRenderUtil* renderUtil = mParentRenderPlugin->GetGraphicsManager()->GetRenderUtil();
        if (renderUtil == nullptr)
        {
            return;
        }

        // set this as the active widget
        // note that this is done in paint() instead of by the plugin because of delay when glwidget::update is called
        MCORE_ASSERT(mParentRenderPlugin->GetActiveViewWidget() == nullptr);
        mParentRenderPlugin->SetActiveViewWidget(mViewWidget);

        // set the background colors
        graphicsManager->SetClearColor(renderOptions->GetBackgroundColor());
        graphicsManager->SetGradientSourceColor(renderOptions->GetGradientSourceColor());
        graphicsManager->SetGradientTargetColor(renderOptions->GetGradientTargetColor());
        graphicsManager->SetUseGradientBackground(mViewWidget->GetRenderFlag(RenderViewWidget::RENDER_USE_GRADIENTBACKGROUND));

        // needed to make multiple viewports working
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_MULTISAMPLE);

        // tell the system about the current viewport
        glViewport(0, 0, aznumeric_cast<GLsizei>(mWidth * devicePixelRatioF()), aznumeric_cast<GLsizei>(mHeight * devicePixelRatioF()));
        renderUtil->SetDevicePixelRatio(aznumeric_cast<float>(devicePixelRatioF()));

        // update advanced render settings
        /*  graphicsManager->SetAdvancedRendering( renderOptions->mEnableAdvancedRendering );
            graphicsManager->SetBloomEnabled    ( renderOptions->mBloomEnabled );
            graphicsManager->SetBloomThreshold  ( renderOptions->mBloomThreshold );
            graphicsManager->SetBloomIntensity  ( renderOptions->mBloomIntensity );
            graphicsManager->SetBloomRadius     ( renderOptions->mBloomRadius );
            graphicsManager->SetDOFEnabled      ( renderOptions->mDOFEnabled );
            graphicsManager->SetDOFFocalDistance( renderOptions->mDOFFocalPoint );
            graphicsManager->SetDOFNear         ( renderOptions->mDOFNear );
            graphicsManager->SetDOFFar          ( renderOptions->mDOFFar );
            graphicsManager->SetDOFBlurRadius   ( renderOptions->mDOFBlurRadius );
        */
        graphicsManager->SetRimAngle        (renderOptions->GetRimAngle());
        graphicsManager->SetRimIntensity    (renderOptions->GetRimIntensity());
        graphicsManager->SetRimWidth        (renderOptions->GetRimWidth());
        graphicsManager->SetRimColor        (renderOptions->GetRimColor());
        graphicsManager->SetMainLightAngleA (renderOptions->GetMainLightAngleA());
        graphicsManager->SetMainLightAngleB (renderOptions->GetMainLightAngleB());
        graphicsManager->SetMainLightIntensity(renderOptions->GetMainLightIntensity());
        graphicsManager->SetSpecularIntensity(renderOptions->GetSpecularIntensity());

        // update the camera
        UpdateCamera();

        graphicsManager->SetCamera(mCamera);

        graphicsManager->BeginRender();

        // render grid
        RenderGrid();

        // render characters
        RenderActorInstances();

        // iterate through all plugins and render their helper data
        RenderCustomPluginData();

        // disable backface culling after rendering the actors
        glDisable(GL_CULL_FACE);

        // render the gizmos
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        RenderWidget::RenderManipulators();

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);

        graphicsManager->EndRender();

        // render overlay
        // do this in the very end as we're clearing the depth buffer here

        // render axis on the bottom left which shows the current orientation of the camera relative to the global axis
        glPushAttrib(GL_ENABLE_BIT);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);

        MCommon::Camera* camera = mCamera;
        if (mCamera->GetType() == MCommon::OrthographicCamera::TYPE_ID)
        {
            camera = mAxisFakeCamera;
        }
        graphicsManager->SetCamera(camera);

        RenderWidget::RenderAxis();

        graphicsManager->SetCamera(mCamera);

        glPopAttrib();

        // render node filter string
        RenderWidget::RenderNodeFilterString();

        // render the border around the render view
        if (EMotionFX::GetRecorder().GetIsRecording() == false && EMotionFX::GetRecorder().GetIsInPlayMode() == false)
        {
            if (mParentRenderPlugin->GetFocusViewWidget() == mViewWidget)
            {
                RenderBorder(MCore::RGBAColor(1.0f, 0.647f, 0.0f));
            }
            else
            {
                RenderBorder(MCore::RGBAColor(0.0f, 0.0f, 0.0f));
            }
        }
        else
        {
            if (EMotionFX::GetRecorder().GetIsRecording())
            {
                renderUtil->RenderText(5, 5, "RECORDING MODE", MCore::RGBAColor(0.8f, 0.0f, 0.0f), 9.0f);
                RenderBorder(MCore::RGBAColor(0.8f, 0.0f, 0.0f));
            }
            else
            if (EMotionFX::GetRecorder().GetIsInPlayMode())
            {
                renderUtil->RenderText(5, 5, "PLAYBACK MODE", MCore::RGBAColor(0.0f, 0.8f, 0.0f), 9.0f);
                RenderBorder(MCore::RGBAColor(0.0f, 0.8f, 0.0f));
            }
        }

        // makes no GL context the current context, needed in multithreaded environments
        //doneCurrent(); // Ben: results in a white screen
        mParentRenderPlugin->SetActiveViewWidget(nullptr);

        painter.endNativePainting();

        if (renderOptions->GetShowFPS())
        {
            const float renderTime = mRenderTimer.GetDeltaTimeInSeconds() * 1000.0f;

            // get the time delta between the current time and the last frame
            const float perfTimeDelta = mPerfTimer.StampAndGetDeltaTimeInSeconds();

            static float fpsTimeElapsed = 0.0f;
            static uint32 fpsNumFrames = 0;
            static uint32 lastFPS = 0;
            fpsTimeElapsed += perfTimeDelta;
            fpsNumFrames++;
            if (fpsTimeElapsed > 1.0f)
            {
                lastFPS         = fpsNumFrames;
                fpsTimeElapsed  = 0.0f;
                fpsNumFrames    = 0;
            }

            static AZStd::string perfTempString;
            perfTempString = AZStd::string::format("%d FPS (%.1f ms)", lastFPS, renderTime);

            // initialize the painter and get the font metrics
            //painter.setBrush( Qt::NoBrush );
            //painter.setPen( QColor(130, 130, 130) );
            //painter.setFont( mFont );
            EMStudioManager::RenderText(painter, perfTempString.c_str(), QColor(150, 150, 150), mFont, *mFontMetrics, Qt::AlignRight, QRect(width() - 55, height() - 20, 50, 20));
            //painter.drawText( QPoint(width() - 133, height() - 14), perfTempString.AsChar() );
        }
    }


    void GLWidget::RenderBorder(const MCore::RGBAColor& color)
    {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0f, mWidth, mHeight, 0.0f, 0.0f, 1.0f);
        glMatrixMode (GL_MODELVIEW);
        glLoadIdentity();
        //glTranslatef(0.375f, 0.375f, 0.0f);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);

        glLineWidth(3.0f);

        glColor3f(color.r, color.g, color.b);
        glBegin(GL_LINES);
        // left
        glVertex2f(0.0f, 0.0f);
        glVertex2f(0.0f, aznumeric_cast<GLfloat>(mHeight));
        // bottom
        glVertex2f(0.0f, aznumeric_cast<GLfloat>(mHeight));
        glVertex2f(aznumeric_cast<GLfloat>(mWidth), aznumeric_cast<GLfloat>(mHeight));
        // top
        glVertex2f(0.0f, 0.0f);
        glVertex2f(aznumeric_cast<GLfloat>(mWidth), 0);
        // right
        glVertex2f(aznumeric_cast<GLfloat>(mWidth), 0.0f);
        glVertex2f(aznumeric_cast<GLfloat>(mWidth), aznumeric_cast<float>(mHeight));
        glEnd();

        glLineWidth(1.0f);
    }


    void GLWidget::focusInEvent(QFocusEvent* event)
    {
        MCORE_UNUSED(event);
        mParentRenderPlugin->SetFocusViewWidget(mViewWidget);
        grabKeyboard();
    }


    void GLWidget::focusOutEvent(QFocusEvent* event)
    {
        MCORE_UNUSED(event);
        mParentRenderPlugin->SetFocusViewWidget(nullptr);
        releaseKeyboard();
    }
} // namespace EMStudio

#include <EMotionFX/Tools/EMotionStudio/Plugins/RenderPlugins/Source/OpenGLRender/moc_GLWidget.cpp>
