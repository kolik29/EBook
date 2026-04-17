import WifiIcon from '@mui/icons-material/Wifi';
import WifiOffIcon from '@mui/icons-material/WifiOff';
import ScreenRotationIcon from '@mui/icons-material/ScreenRotation';
import UploadIcon from '@mui/icons-material/Upload';
import RefreshIcon from '@mui/icons-material/Refresh';
import { Box, IconButton } from '@mui/material';
import clsx from 'clsx';
import { useRef, useState } from 'react';
import { disableWifiAction, refreshDisplayAction, rotateDisplayAction } from '../../../api/device';
import { uploadBook } from '../../../api/books';
import { useBooksStore } from '../../../store/books';

const ICON_SIZE = 30;
const iconSx = { fontSize: ICON_SIZE };

const ActionBar = () => {
    const [disabledWiFi, setDisabledWiFi] = useState(false);
    const [rotateDisplay, setRotateDisplay] = useState(false);
    const [refreshDisplay, setRefreshDisplay] = useState(false);
    const [uploading, setUploading] = useState(false);

    const fileInputRef = useRef<HTMLInputElement>(null);

    const loadBooks = useBooksStore((state) => state.loadBooks);

    const handleDisableWiFi = () => {
        disableWifiAction();
        setDisabledWiFi(true);
    };

    const handleRotateDisplay = () => {
        rotateDisplayAction();
        setRotateDisplay(true);
    };

    const handleRefreshDisplay = () => {
        refreshDisplayAction();
        setRefreshDisplay(true);
    };

    const handleUploadBook = () => {
        if (uploading) {
            return;
        }

        fileInputRef.current?.click();
    };

    const handleFileChange = async (e: React.ChangeEvent<HTMLInputElement>) => {
        const files = Array.from(e.target.files ?? []);

        if (!files.length) {
            return;
        }

        setUploading(true);

        try {
            await Promise.all(files.map((file) => uploadBook(file)));

            await loadBooks();

            console.log('Uploaded files:', files);
        } catch (error) {
            console.error(error);
        } finally {
            setUploading(false);

            if (fileInputRef.current) {
                fileInputRef.current.value = '';
            }
        }
    };

    return (
        <Box sx={{ p: 1 }} className={clsx('display__flex justify-content__space-around')}>
            <IconButton onClick={handleDisableWiFi} disabled={uploading}>
                {disabledWiFi ? <WifiOffIcon sx={iconSx} /> : <WifiIcon sx={iconSx} />}
            </IconButton>

            <IconButton onClick={handleRotateDisplay} disabled={uploading}>
                <ScreenRotationIcon
                    sx={iconSx}
                    className={clsx({ 'spin': rotateDisplay })}
                    onAnimationEnd={() => setRotateDisplay(false)}
                />
            </IconButton>

            <IconButton onClick={handleRefreshDisplay} disabled={uploading}>
                <RefreshIcon
                    sx={iconSx}
                    className={clsx({ 'spin': refreshDisplay })}
                    onAnimationEnd={() => setRefreshDisplay(false)}
                />
            </IconButton>

            <IconButton onClick={handleUploadBook} disabled={uploading}>
                <UploadIcon
                    sx={iconSx}
                    className={clsx({ 'spin': uploading })}
                />
            </IconButton>

            <input
                ref={fileInputRef}
                type="file"
                hidden
                multiple
                onChange={handleFileChange}
            />
        </Box>
    );
};

export default ActionBar;