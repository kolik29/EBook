import styles from './styles.module.sass';
import { Box, Stack, Typography } from '@mui/material';
import CheckIcon from '@mui/icons-material/Check';
import clsx from 'clsx';
import BookCover from '../BookCover';

const BookCard = ({ img, title, active, onClick }: { img: string; title: string; active: boolean, onClick?: () => void }) => {
    return (
        <Box className={clsx('display__grid', 'height__100', styles.item)} onClick={onClick}>
            <Box className={clsx('position__relative')}>
                <BookCover
                    src={img}
                    alt={title}
                    borderRadius="16px 16px 0 0"
                />
                {active &&
                <Stack
                    sx={{
                        justifyContent: 'center',
                        alignItems: 'center',
                    }}
                    className={clsx('position__absolute', 'width__100', 'height__100', 'top__0', 'left__0', styles.active)}>
                        <CheckIcon />
                </Stack>}
            </Box>
            <Typography variant='h2' sx={{ fontSize: 16 }} className={clsx('text-align__center', styles.title)}>
                {title}
            </Typography>
        </Box>
    )
};

export default BookCard;
