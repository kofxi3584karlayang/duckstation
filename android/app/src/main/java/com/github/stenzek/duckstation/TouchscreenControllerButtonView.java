package com.github.stenzek.duckstation;

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.drawable.Drawable;
import android.util.AttributeSet;
import android.util.TypedValue;
import android.view.HapticFeedbackConstants;
import android.view.View;

/**
 * TODO: document your custom view class.
 */
public final class TouchscreenControllerButtonView extends View {
    public enum Hotkey
    {
        NONE,
        FAST_FORWARD,
        ANALOG_TOGGLE,
        OPEN_PAUSE_MENU,
    }

    private Drawable mUnpressedDrawable;
    private Drawable mPressedDrawable;
    private boolean mPressed = false;
    private boolean mHapticFeedback = false;
    private int mControllerIndex = -1;
    private int mButtonCode = -1;
    private int mAutoFireSlot = -1;
    private Hotkey mHotkey = Hotkey.NONE;
    private String mConfigName;
    private boolean mDefaultVisibility = true;
    private boolean mIsGlidable = true;

    public TouchscreenControllerButtonView(Context context) {
        super(context);
        init(context, null, 0);
    }

    public TouchscreenControllerButtonView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(context, attrs, 0);
    }

    public TouchscreenControllerButtonView(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
        init(context, attrs, defStyle);
    }

    private void init(Context context, AttributeSet attrs, int defStyle) {
        // Load attributes
        final TypedArray a = getContext().obtainStyledAttributes(
                attrs, R.styleable.TouchscreenControllerButtonView, defStyle, 0);

        if (a.hasValue(R.styleable.TouchscreenControllerButtonView_unpressedDrawable)) {
            mUnpressedDrawable = a.getDrawable(R.styleable.TouchscreenControllerButtonView_unpressedDrawable);
            mUnpressedDrawable.setCallback(this);
        }

        if (a.hasValue(R.styleable.TouchscreenControllerButtonView_pressedDrawable)) {
            mPressedDrawable = a.getDrawable(R.styleable.TouchscreenControllerButtonView_pressedDrawable);
            mPressedDrawable.setCallback(this);
        }

        a.recycle();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        int leftBounds = 0;
        int rightBounds = leftBounds + getWidth();
        int topBounds = 0;
        int bottomBounds = topBounds + getHeight();

        if (mPressed) {
            final int expandSize = (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP,
                    10.0f, getResources().getDisplayMetrics());
            leftBounds -= expandSize;
            rightBounds += expandSize;
            topBounds -= expandSize;
            bottomBounds += expandSize;
        }

        // Draw the example drawable on top of the text.
        Drawable drawable = mPressed ? mPressedDrawable : mUnpressedDrawable;
        if (drawable != null) {
            drawable.setBounds(leftBounds, topBounds, rightBounds, bottomBounds);
            drawable.draw(canvas);
        }
    }

    public boolean isPressed() {
        return mPressed;
    }

    public void setPressed(boolean pressed) {
        if (pressed == mPressed)
            return;

        mPressed = pressed;
        invalidate();
        updateControllerState();

        if (mHapticFeedback) {
            performHapticFeedback(pressed ? HapticFeedbackConstants.VIRTUAL_KEY : HapticFeedbackConstants.VIRTUAL_KEY_RELEASE);
        }
    }

    public void setButtonCode(int controllerIndex, int code) {
        mControllerIndex = controllerIndex;
        mButtonCode = code;
    }

    public void setAutoFireSlot(int controllerIndex, int slot) {
        mControllerIndex = controllerIndex;
        mAutoFireSlot = slot;
    }

    public void setHotkey(Hotkey hotkey) {
        mHotkey = hotkey;
    }

    public String getConfigName() {
        return mConfigName;
    }
    public void setConfigName(String name) {
        mConfigName = name;
    }

    public boolean getIsGlidable() { return mIsGlidable; }
    public void setIsGlidable(boolean isGlidable) { mIsGlidable = isGlidable; }

    public boolean getDefaultVisibility() { return mDefaultVisibility; }
    public void setDefaultVisibility(boolean visibility) { mDefaultVisibility = visibility; }

    public void setHapticFeedback(boolean enabled) {
        mHapticFeedback = enabled;
    }

    private void updateControllerState() {
        final AndroidHostInterface hi = AndroidHostInterface.getInstance();
        if (mButtonCode >= 0)
            hi.setControllerButtonState(mControllerIndex, mButtonCode, mPressed);
        if (mAutoFireSlot >= 0)
            hi.setControllerAutoFireState(mControllerIndex, mAutoFireSlot, mPressed);

        switch (mHotkey)
        {
            case FAST_FORWARD:
                hi.setFastForwardEnabled(mPressed);
                break;

            case ANALOG_TOGGLE: {
                if (!mPressed)
                    hi.toggleControllerAnalogMode();
            }
            break;

            case OPEN_PAUSE_MENU: {
                if (!mPressed)
                    hi.getEmulationActivity().openPauseMenu();
            }
            break;

            case NONE:
            default:
                break;
        }
    }

    public Drawable getPressedDrawable() {
        return mPressedDrawable;
    }

    public void setPressedDrawable(Drawable pressedDrawable) {
        mPressedDrawable = pressedDrawable;
    }

    public Drawable getUnpressedDrawable() {
        return mUnpressedDrawable;
    }

    public void setUnpressedDrawable(Drawable unpressedDrawable) {
        mUnpressedDrawable = unpressedDrawable;
    }
}
