import styles from './styles.module.sass';
import { Box, IconButton, InputAdornment, TextField, Typography } from '@mui/material';
import SkipPreviousIcon from '@mui/icons-material/SkipPrevious';
import SkipNextIcon from '@mui/icons-material/SkipNext';
import PlayArrowIcon from '@mui/icons-material/PlayArrow';
import DeleteIcon from '@mui/icons-material/Delete';
import clsx from 'clsx';
import { useEffect, useMemo, useState } from 'react';
import { useBooksStore } from '../../../store/books';
import { deleteBook, updateCurrentPage } from '../../../api/books';
import BookCover from '../BookCover';

const SelectedBookPanel = () => {
    const [page, setPage] = useState(1);
    const books = useBooksStore((state) => state.books);

    const reloadBooks = useBooksStore((state) => state.reloadBooks);

    const selectedBook = useMemo(() => {
        return books.find((book) => book.active) ?? null;
    }, [books]);

    useEffect(() => {
        setPage(selectedBook?.page.current ?? 1);
    }, [selectedBook?.id, selectedBook?.page.current]);

    if (!selectedBook) {
        return null;
    }

    const handlePageChange = (e: any) => {
        setPage(e.target.value);
    }

    const handlePrevPage = () => {
        if (page === 1) return;
        setPage(page - 1);
    }

    const handleNextPage = async () => {
        setPage(page + 1);
        updateCurrentPage(selectedBook.id, page + 1);
    }

    const handleSendPage = () => {
        updateCurrentPage(selectedBook.id, page);
    }

    const handleDeleteBook = async () => {
        if (confirm('Are you sure you want to delete this book?')) {
            await deleteBook(selectedBook.id);
            await reloadBooks();
        }
    }

    return (
        <Box sx={{ p: 2 }} className={clsx('position__relative', 'height__100', 'display__flex', 'flex-direction__column', 'align-items__center', styles.leftSide)}>
            <Box className={clsx(styles.cover)}>
                <BookCover
                    src={selectedBook.img}
                    alt={selectedBook.title}
                />
            </Box>
            <Box className='display__flex flex-direction__column align-items__center'>
                <Typography
                    variant='h1'
                    sx={{
                        fontSize: { xs: 24, md: 36 },
                        marginTop: { xs: 2, md: 4 },
                        marginBottom: { xs: 2, md: 4 }
                    }}
                    className={clsx('text-align__center', styles.title)}
                >
                    {selectedBook.title}
                </Typography>
                <Typography variant='body1' sx={{ marginBottom: { xs: 2, md: 4 } }}>
                    {selectedBook.author}
                </Typography>
                <Box className={clsx('display__flex', 'align-items__center')}>
                    <TextField
                        variant='outlined'
                        size='small'
                        defaultValue={1}
                        value={page}
                        type='number'
                        slotProps={{ input: { endAdornment: <InputAdornment position='start'>/ {selectedBook.page.total}</InputAdornment> } }}
                        sx={{ width: '140px' }}
                        onChange={handlePageChange}
                    />
                </Box>
                <Box className={clsx(styles.actions)}>
                    <IconButton onClick={handlePrevPage}>
                        <SkipPreviousIcon sx={{ fontSize: { xs: 48, md: 48 } }} />
                    </IconButton>
                    <IconButton onClick={handleSendPage}>
                        <PlayArrowIcon sx={{ fontSize: { xs: 48, md: 48 } }} />
                    </IconButton>
                    <IconButton onClick={handleNextPage}>
                        <SkipNextIcon sx={{ fontSize: { xs: 48, md: 48 } }} />
                    </IconButton>
                </Box>
            </Box>
            <Box className={clsx('position__absolute', styles.delete)}>
                <IconButton onClick={handleDeleteBook}>
                    <DeleteIcon color='error' />
                </IconButton>
            </Box>
        </Box>
    )
};

export default SelectedBookPanel;
