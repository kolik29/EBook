import styles from './styles.module.sass';
import BookItem from '../BookCard';
import { Swiper, SwiperSlide } from 'swiper/react';
import { FreeMode, Mousewheel } from 'swiper/modules';
import 'swiper/css';
import { Box, CircularProgress, Stack, Typography } from '@mui/material';
import clsx from 'clsx';
import { useEffect, useMemo } from 'react';
import { useBooksStore } from '../../../store/books';

const BookCatalog = () => {
    const books = useBooksStore((state) => state.books);
    const loading = useBooksStore((state) => state.loading);
    const query = useBooksStore((state) => state.query);
    const loadBooks = useBooksStore((state) => state.loadBooks);
    const selectBook = useBooksStore((state) => state.selectBook);

    useEffect(() => {
        loadBooks();
    }, [loadBooks]);

    const filteredBooks = useMemo(() => {
        const normalizedQuery = query.trim().toLowerCase();

        if (!normalizedQuery) {
            return books;
        }

        return books.filter((book) => {
            return (
                book.title.toLowerCase().includes(normalizedQuery) ||
                book.author.toLowerCase().includes(normalizedQuery)
            );
        });
    }, [books, query]);

    if (loading) {
        return (
            <Stack className={clsx('height__100', 'align-items__center', 'justify-content__center')}>
                <CircularProgress />
            </Stack>
        );
    }

    if (!filteredBooks.length) {
        return (
            <Stack className={clsx('height__100', 'align-items__center', 'justify-content__center')}>
                <Typography>No books found</Typography>
            </Stack>
        );
    }

    const mobileSlides = filteredBooks.map((book) => (
        <SwiperSlide key={book.id} className={clsx(styles.slideMobile)}>
            <BookItem
                img={book.img}
                title={book.title}
                active={book.active}
                onClick={() => selectBook(book.id)}
            />
        </SwiperSlide>
    ));

    return (
        <>
            <Swiper
                className={clsx('display__none--xs', styles.swiperDesktop)}
                modules={[FreeMode, Mousewheel]}
                direction='vertical'
                slidesPerView='auto'
                freeMode={{
                    enabled: true,
                    sticky: false,
                    momentum: true,
                }}
                mousewheel={{
                    enabled: true,
                    forceToAxis: true,
                    releaseOnEdges: true,
                    sensitivity: 1,
                }}
                watchOverflow
                observer
                observeParents
            >
                <SwiperSlide className={clsx(styles.desktopSlide)}>
                    <Box className={clsx(styles.catalogGrid)}>
                        {filteredBooks.map((book) => (
                            <BookItem
                                key={book.id}
                                img={book.img}
                                title={book.title}
                                active={book.active}
                                onClick={() => selectBook(book.id)}
                            />
                        ))}
                    </Box>
                </SwiperSlide>
            </Swiper>

            <Swiper
                className={clsx('display__none--md', styles.swiperMobile)}
                modules={[FreeMode]}
                slidesPerView='auto'
                spaceBetween={16}
                freeMode={{
                    enabled: true,
                    momentum: true,
                }}
            >
                {mobileSlides}
            </Swiper>
        </>
    );
};

export default BookCatalog;