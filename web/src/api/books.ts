import type { Book, BookPagination } from '../types/book';
import { API_URL } from './config';

const mockBooks: Book[] = Array.from({ length: 200 }, (_, index) => ({
    id: index + 1,
    title: "Harry Potter " + (index + 1),
    author: 'JK Rowling',
    img: index === 3 ? 'https://images.booksense.com/images/403/353/9780590353403.jpg234' : 'https://images.booksense.com/images/403/353/9780590353403.jpg',
    active: index === 5 ? true : false,
    page: {
        total: 400,
        current: index === 5 ? 123 : 1
    }
}));

export const getBooks = async (): Promise<Book[]> => {
    try {
        const res = await fetch(`${API_URL}/books`);

        if (!res.ok) {
            throw new Error('Failed to get books');
        }

        return await res.json();
    } catch {
        return mockBooks;
    }
};

export const uploadBook = async (file: File): Promise<void> => {
    try {
        const formData = new FormData();
        formData.append('file', file);

        const res = await fetch(`${API_URL}/books/upload`, {
            method: 'POST',
            body: formData,
        });

        if (!res.ok) {
            throw new Error('Failed to upload book');
        }
    } catch (error) {
        console.error(error);
    }
};

export const deleteBook = async (id: number): Promise<void> => {
    try {
        const res = await fetch(`${API_URL}/books/${id}`, {
            method: 'DELETE',
        });

        if (!res.ok) {
            throw new Error('Failed to delete book');
        }
    } catch (error) {
        console.error(error);
    }
};

export const updateCurrentPage = async (id: number, currentPage: number): Promise<void> => {
    try {
        const res = await fetch(`${API_URL}/books/${id}`, {
            method: 'PATCH',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({ currentPage }),
        });

        if (!res.ok) {
            throw new Error('Failed to update current page');
        }
    } catch (error) {
        console.error(error);
    }
};

export const getBookPagination = async (id: number): Promise<BookPagination | null> => {
    try {
        const res = await fetch(`${API_URL}/books/${id}/pagination`);

        if (!res.ok) {
            throw new Error('Failed to get book pagination');
        }

        return await res.json();
    } catch (error) {
        console.error(error);
        return null;
    }
};