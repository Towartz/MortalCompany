package com.android.support;

import android.annotation.SuppressLint;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.util.Log;
import android.view.WindowManager;
import android.widget.Toast;

public class DialogHelper {

    private static final String TAG = "DialogHelper";

    public static void showDialogWithLink(Context context, String title, String message, String LinkBtnTitle, String CloseBtnTitle, int sec, final String url) {
        // Service contexts can't show dialogs without a window token.
        // Attempt with TYPE_APPLICATION_OVERLAY as fallback.
        boolean shown = false;
        Throwable lastError = null;

        for (int attempt = 0; attempt < 2 && !shown; attempt++) {
            try {
                AlertDialog.Builder builder = new AlertDialog.Builder(context)
                        .setTitle(title)
                        .setMessage(message);

                if (url != null) {
                    builder.setPositiveButton(LinkBtnTitle, new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog, int which) {
                            openUrlSafely(context, url);
                        }
                    });
                }

                final AlertDialog dialog = builder.create();

                String closeInitTitle = CloseBtnTitle;
                if (sec > 0) {
                    closeInitTitle = closeInitTitle + "(" + sec + ")";
                }

                dialog.setButton(DialogInterface.BUTTON_NEGATIVE, closeInitTitle,
                        new DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(DialogInterface dialog, int which) {
                            }
                        });

                // On second attempt, use overlay window type (requires overlay permission)
                if (attempt == 1 && dialog.getWindow() != null) {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                        dialog.getWindow().setType(WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY);
                    } else {
                        dialog.getWindow().setType(WindowManager.LayoutParams.TYPE_SYSTEM_ALERT);
                    }
                }

                dialog.show();
                shown = true;

                if (sec > 0) startCountdown(dialog, CloseBtnTitle, sec);
            } catch (WindowManager.BadTokenException e) {
                lastError = e;
                Log.w(TAG, "Dialog show failed (attempt " + attempt + "): " + e.getMessage());
            } catch (Exception e) {
                lastError = e;
                Log.e(TAG, "Dialog show failed: " + e.getMessage());
                break; // Non-BadTokenException — don't retry
            }
        }

        if (!shown) {
            Log.e(TAG, "Could not show dialog after 2 attempts", lastError);
            try {
                Toast.makeText(context, title + ": " + message, Toast.LENGTH_LONG).show();
            } catch (Exception ignored) {
            }
        }
    }

    private static void startCountdown(final AlertDialog dialog, String CloseBtnTitle, final int seconds) {
        final Handler handler = new Handler();
        final Runnable countdownRunnable = new Runnable() {
            int remainingTime = seconds;

            @SuppressLint("SetTextI18n")
            @Override
            public void run() {
                if (remainingTime > 0 && dialog != null && dialog.isShowing()) {
                    dialog.getButton(AlertDialog.BUTTON_NEGATIVE)
                            .setText(CloseBtnTitle + "(" + remainingTime + ")");
                    remainingTime--;
                    handler.postDelayed(this, 1000);
                } else if (dialog != null && dialog.isShowing()) {
                    dialog.getButton(AlertDialog.BUTTON_NEGATIVE).performClick();
                }
            }
        };

        handler.post(countdownRunnable);
    }

    @SuppressLint("QueryPermissionsNeeded")
    public static void openUrlSafely(Context context, String url) {
        if (url == null || url.trim().isEmpty()) {
            return;
        }

        if (!url.startsWith("http://") && !url.startsWith("https://")) {
            url = "http://" + url;
        }

        try {
            Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);

            if (intent.resolveActivity(context.getPackageManager()) != null) {
                context.startActivity(intent);
            } else {
                Intent browserIntent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
                browserIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                browserIntent.setPackage("com.android.chrome");

                if (browserIntent.resolveActivity(context.getPackageManager()) != null) {
                    context.startActivity(browserIntent);
                } else {
                    browserIntent.setPackage(null);
                    context.startActivity(browserIntent);
                }
            }
        } catch (Exception e) {
            Toast.makeText(context, e.toString(), Toast.LENGTH_SHORT).show();
        }
    }
}