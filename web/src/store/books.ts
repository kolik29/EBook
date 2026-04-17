import { create } from 'zustand';
import type { Book } from '../types/book';
import { getBooks } from '../api/books';

type BooksStore = {
    books: Book[];
    loading: boolean;
    loaded: boolean;
    query: string;
    setQuery: (query: string) => void;
    loadBooks: () => Promise<void>;
    selectBook: (id: number) => void;
};

export const useBooksStore = create<BooksStore>()((set, get) => ({
    books: [],
    loading: false,
    loaded: false,
    query: '',

    setQuery: (query) => set({ query }),

    loadBooks: async () => {
        const { loading, loaded } = get();

        if (loading || loaded) {
            return;
        }

        set({ loading: true });

        try {
            const books = await getBooks();
            set({
                books,
                loaded: true,
            });
        } finally {
            set({ loading: false });
        }
    },

    selectBook: (id) =>
        set((state) => ({
            books: state.books.map((book) => ({
                ...book,
                active: book.id === id,
            })),
        })),
}));