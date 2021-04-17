package com.github.stenzek.duckstation;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.ImageDecoder;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.MediaStore;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;

/**
 * File helper class - used to bridge native code to Java storage access framework APIs.
 */
public class FileHelper {
    /**
     * Native filesystem flags.
     */
    public static final int FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY = 1;
    public static final int FILESYSTEM_FILE_ATTRIBUTE_READ_ONLY = 2;
    public static final int FILESYSTEM_FILE_ATTRIBUTE_COMPRESSED = 4;

    /**
     * Native filesystem find result flags.
     */
    public static final int FILESYSTEM_FIND_RECURSIVE = (1 << 0);
    public static final int FILESYSTEM_FIND_RELATIVE_PATHS = (1 << 1);
    public static final int FILESYSTEM_FIND_HIDDEN_FILES = (1 << 2);
    public static final int FILESYSTEM_FIND_FOLDERS = (1 << 3);
    public static final int FILESYSTEM_FIND_FILES = (1 << 4);
    public static final int FILESYSTEM_FIND_KEEP_ARRAY = (1 << 5);

    /**
     * Projection used when searching for files.
     */
    private static final String[] findProjection = new String[]{
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE,
            DocumentsContract.Document.COLUMN_LAST_MODIFIED
    };

    private final Context context;
    private final ContentResolver contentResolver;

    /**
     * File helper class - used to bridge native code to Java storage access framework APIs.
     *
     * @param context Context in which to perform file actions as.
     */
    public FileHelper(Context context) {
        this.context = context;
        this.contentResolver = context.getContentResolver();
    }

    /**
     * Reads the specified file as a string, under the specified context.
     *
     * @param context context to access file under
     * @param uri     uri to write data to
     * @param maxSize maximum file size to read
     * @return String containing the file data, otherwise null
     */
    public static String readStringFromUri(final Context context, final Uri uri, int maxSize) {
        InputStream stream = null;
        try {
            stream = context.getContentResolver().openInputStream(uri);
        } catch (FileNotFoundException e) {
            return null;
        }

        StringBuilder os = new StringBuilder();
        try {
            char[] buffer = new char[1024];
            InputStreamReader reader = new InputStreamReader(stream, Charset.forName(StandardCharsets.UTF_8.name()));
            int len;
            while ((len = reader.read(buffer)) > 0) {
                os.append(buffer, 0, len);
                if (os.length() > maxSize)
                    return null;
            }

            stream.close();
        } catch (IOException e) {
            return null;
        }

        if (os.length() == 0)
            return null;

        return os.toString();
    }

    /**
     * Reads the specified file as a byte array, under the specified context.
     *
     * @param context context to access file under
     * @param uri     uri to write data to
     * @param maxSize maximum file size to read
     * @return byte array containing the file data, otherwise null
     */
    public static byte[] readBytesFromUri(final Context context, final Uri uri, int maxSize) {
        InputStream stream = null;
        try {
            stream = context.getContentResolver().openInputStream(uri);
        } catch (FileNotFoundException e) {
            return null;
        }

        ByteArrayOutputStream os = new ByteArrayOutputStream();
        try {
            byte[] buffer = new byte[512 * 1024];
            int len;
            while ((len = stream.read(buffer)) > 0) {
                os.write(buffer, 0, len);
                if (maxSize > 0 && os.size() > maxSize) {
                    return null;
                }
            }

            stream.close();
        } catch (IOException e) {
            e.printStackTrace();
            return null;
        }

        if (os.size() == 0)
            return null;

        return os.toByteArray();
    }

    /**
     * Writes the specified data to a file referenced by the URI, as the specified context.
     *
     * @param context context to access file under
     * @param uri     uri to write data to
     * @param bytes   data to write file to
     * @return true if write was succesful, otherwise false
     */
    public static boolean writeBytesToUri(final Context context, final Uri uri, final byte[] bytes) {
        OutputStream stream = null;
        try {
            stream = context.getContentResolver().openOutputStream(uri);
        } catch (FileNotFoundException e) {
            e.printStackTrace();
            return false;
        }

        if (bytes != null && bytes.length > 0) {
            try {
                stream.write(bytes);
                stream.close();
            } catch (IOException e) {
                e.printStackTrace();
                return false;
            }
        }

        return true;
    }

    /**
     * Deletes the file referenced by the URI, under the specified context.
     *
     * @param context context to delete file under
     * @param uri     uri to delete
     * @return
     */
    public static boolean deleteFileAtUri(final Context context, final Uri uri) {
        try {
            if (uri.getScheme() == "file") {
                final File file = new File(uri.getPath());
                if (!file.isFile())
                    return false;

                return file.delete();
            }
            return (context.getContentResolver().delete(uri, null, null) > 0);
        } catch (Exception e) {
            e.printStackTrace();
            return false;
        }
    }

    /**
     * Returns the name of the file pointed at by a SAF URI.
     *
     * @param context context to access file under
     * @param uri     uri to retrieve file name for
     * @return the name of the file, or null
     */
    public static String getDocumentNameFromUri(final Context context, final Uri uri) {
        Cursor cursor = null;
        try {
            final String[] proj = {DocumentsContract.Document.COLUMN_DISPLAY_NAME};
            cursor = context.getContentResolver().query(uri, proj, null, null, null);
            final int columnIndex = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DISPLAY_NAME);
            cursor.moveToFirst();
            return cursor.getString(columnIndex);
        } catch (Exception e) {
            e.printStackTrace();
            return null;
        } finally {
            if (cursor != null)
                cursor.close();
        }
    }

    /**
     * Loads a bitmap from the provided SAF URI.
     *
     * @param context context to access file under
     * @param uri     uri to retrieve file name for
     * @return a decoded bitmap for the file, or null
     */
    public static Bitmap loadBitmapFromUri(final Context context, final Uri uri) {
        InputStream stream = null;
        try {
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.P) {
                final ImageDecoder.Source source = ImageDecoder.createSource(context.getContentResolver(), uri);
                return ImageDecoder.decodeBitmap(source);
            } else {
                return MediaStore.Images.Media.getBitmap(context.getContentResolver(), uri);
            }
        } catch (Exception e) {
            e.printStackTrace();
            return null;
        }
    }

    /**
     * Returns the file name component of a path or URI.
     * @param path Path/URI to examine.
     * @return File name component of path/URI.
     */
    public static String getFileNameForPath(String path) {
        if (path.startsWith("content:/") || path.startsWith("file:/")) {
            try {
                final Uri uri = Uri.parse(path);
                final String lastPathSegment = uri.getLastPathSegment();
                if (lastPathSegment != null)
                    path = lastPathSegment;
            } catch (Exception e) {
            }
        }

        int lastSlash = path.lastIndexOf('/');
        if (lastSlash > 0 && lastSlash < path.length() - 1)
            return path.substring(lastSlash + 1);
        else
            return path;
    }

    /**
     * Retrieves a file descriptor for a content URI string. Called by native code.
     *
     * @param uriString string of the URI to open
     * @param mode      Java open mode
     * @return file descriptor for URI, or -1
     */
    public int openURIAsFileDescriptor(String uriString, String mode) {
        try {
            final Uri uri = Uri.parse(uriString);
            final ParcelFileDescriptor fd = contentResolver.openFileDescriptor(uri, mode);
            if (fd == null)
                return -1;
            return fd.detachFd();
        } catch (Exception e) {
            return -1;
        }
    }

    /**
     * Recursively iterates documents in the specified tree, searching for files.
     *
     * @param treeUri    Root tree in which to search for documents.
     * @param documentId Document ID representing the directory to start searching.
     * @param flags      Native search flags.
     * @param results    Cumulative result array.
     */
    private void doFindFiles(Uri treeUri, String documentId, int flags, ArrayList<FindResult> results) {
        try {
            final Uri queryUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, documentId);
            final Cursor cursor = contentResolver.query(queryUri, findProjection, null, null, null);
            final int count = cursor.getCount();

            while (cursor.moveToNext()) {
                try {
                    final String mimeType = cursor.getString(2);
                    final String childDocumentId = cursor.getString(0);
                    final Uri uri = DocumentsContract.buildDocumentUriUsingTree(treeUri, childDocumentId);
                    final long size = cursor.getLong(3);
                    final long lastModified = cursor.getLong(4);

                    if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mimeType)) {
                        if ((flags & FILESYSTEM_FIND_FOLDERS) != 0) {
                            results.add(new FindResult(childDocumentId, uri.toString(), size, lastModified, FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY));
                        }

                        if ((flags & FILESYSTEM_FIND_RECURSIVE) != 0)
                            doFindFiles(treeUri, childDocumentId, flags, results);
                    } else {
                        if ((flags & FILESYSTEM_FIND_FILES) != 0) {
                            results.add(new FindResult(childDocumentId, uri.toString(), size, lastModified, 0));
                        }
                    }
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
            cursor.close();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    /**
     * Recursively iterates documents in the specified URI, searching for files.
     *
     * @param uriString URI containing directory to search.
     * @param flags     Native filter flags.
     * @return Array of find results.
     */
    public FindResult[] findFiles(String uriString, int flags) {
        try {
            final Uri fullUri = Uri.parse(uriString);
            final String documentId = DocumentsContract.getTreeDocumentId(fullUri);
            final ArrayList<FindResult> results = new ArrayList<>();
            doFindFiles(fullUri, documentId, flags, results);
            if (results.isEmpty())
                return null;

            final FindResult[] resultsArray = new FindResult[results.size()];
            results.toArray(resultsArray);
            return resultsArray;
        } catch (Exception e) {
            e.printStackTrace();
            return null;
        }
    }

    /**
     * Java class containing the data for a file in a find operation.
     */
    public static class FindResult {
        public String name;
        public String relativeName;
        public long size;
        public long modifiedTime;
        public int flags;

        public FindResult(String relativeName, String name, long size, long modifiedTime, int flags) {
            this.relativeName = relativeName;
            this.name = name;
            this.size = size;
            this.modifiedTime = modifiedTime;
            this.flags = flags;
        }
    }
}
