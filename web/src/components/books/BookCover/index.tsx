import { useEffect, useState } from "react";
import MenuBookIcon from "@mui/icons-material/MenuBook";
import { Box } from "@mui/material";
import type { BookCoverProps } from "../../../types/book";

const BookCover = ({
    src,
    alt,
    width = "100%",
    height = "100%"
}: BookCoverProps) => {
    const [hasError, setHasError] = useState(!src);

    useEffect(() => {
        setHasError(!src);
    }, [src]);

    if (hasError) {
        return (
            <Box
                sx={{
                    width,
                    height,
                    display: "flex",
                    alignItems: "center",
                    justifyContent: "center",
                    bgcolor: "action.hover",
                }}
            >
                <MenuBookIcon sx={{ fontSize: 40, opacity: 0.6 }} />
            </Box>
        );
    }

    return (
        <Box
            component="img"
            src={src ?? undefined}
            alt={alt}
            onError={() => setHasError(true)}
            sx={{
                width,
                height,
                maxWidth: "100%",
                maxHeight: "100%",
                objectFit: "contain",
                objectPosition: "center",
                display: "block",
            }}
        />
    );
};

export default BookCover;
