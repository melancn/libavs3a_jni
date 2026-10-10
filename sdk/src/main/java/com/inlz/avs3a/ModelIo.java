package com.inlz.avs3a;

import java.io.InputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.FileOutputStream;
import java.io.File;
import java.io.RandomAccessFile;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

import android.content.Context;

final class ModelIo {
    private static final int MAX_MODEL_SIZE = 79930;
    private static final String EXPECTED_SHA256 = "55d56a17dbfa22da21f2fa945ebe13824d246fe8b2669a63e0440316282ae068";

    static VerifiedModel stageModel(Context appContext, File root, String vendorId, ModelSource source)
            throws Avs3Exception, IOException {
        File modelDir = new File(root, "avs3a" + File.separator + vendorId);
        File target = new File(modelDir, "model.bin");
        File lockFile = new File(modelDir, ".lock");

        modelDir.mkdirs();

        try (RandomAccessFile lockRaf = new RandomAccessFile(lockFile, "rw");
             FileChannel channel = lockRaf.getChannel();
             FileLock lock = channel.tryLock()) {

            if (lock == null) {
                throw new Avs3Exception(Avs3Error.MODEL_IO_FAILED, "concurrent model preparation");
            }

            if (target.exists() && target.isFile()) {
                String existingSha = sha256(target);
                if (EXPECTED_SHA256.equals(existingSha)) {
                    return new VerifiedModel(appContext, target.getAbsolutePath(), vendorId, existingSha);
                }
                throw new Avs3Exception(Avs3Error.MODEL_CORRUPT);
            }

            File tempFile = new File(modelDir, ".model.tmp." + Thread.currentThread().getId());
            try (InputStream is = source.open();
                 FileOutputStream fos = new FileOutputStream(tempFile)) {
                byte[] buf = new byte[8192];
                int total = 0;
                int n;
                while ((n = is.read(buf)) > 0) {
                    total += n;
                    if (total > MAX_MODEL_SIZE) {
                        tempFile.delete();
                        throw new Avs3Exception(Avs3Error.MODEL_CORRUPT, "model too large");
                    }
                    fos.write(buf, 0, n);
                }
                fos.flush();
                fos.getFD().sync();
            }

            if (tempFile.length() != MAX_MODEL_SIZE) {
                tempFile.delete();
                throw new Avs3Exception(Avs3Error.MODEL_CORRUPT, "model size mismatch");
            }

            String sha = sha256(tempFile);
            if (!EXPECTED_SHA256.equals(sha)) {
                tempFile.delete();
                throw new Avs3Exception(Avs3Error.MODEL_CORRUPT);
            }

            if (!tempFile.renameTo(target)) {
                tempFile.delete();
                throw new Avs3Exception(Avs3Error.MODEL_IO_FAILED);
            }

            return new VerifiedModel(appContext, target.getAbsolutePath(), vendorId, sha);
        } finally {
            File tempFile = new File(modelDir, ".model.tmp." + Thread.currentThread().getId());
            if (tempFile.exists()) tempFile.delete();
        }
    }

    private static String sha256(File file) throws IOException {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            try (java.io.FileInputStream fis = new java.io.FileInputStream(file)) {
                byte[] buf = new byte[8192];
                int n;
                while ((n = fis.read(buf)) > 0) {
                    md.update(buf, 0, n);
                }
            }
            byte[] digest = md.digest();
            StringBuilder sb = new StringBuilder(64);
            for (byte b : digest) {
                sb.append(String.format("%02x", b & 0xff));
            }
            return sb.toString();
        } catch (NoSuchAlgorithmException e) {
            throw new IOException("SHA-256 unavailable", e);
        }
    }
}
